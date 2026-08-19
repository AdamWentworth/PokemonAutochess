using System.Buffers.Binary;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;

const string PackedFormat =
    "phlosion-za-ui-offscreen-rgba16f-cube-mips-packed-v1";

var options = ParseOptions(args);
var sourcePath = Required(options, "source");
var outputPath = Required(options, "output");
var manifestPath = Required(options, "manifest");

var source = File.ReadAllBytes(sourcePath);
var decoded = Decode(source);
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputPath))!);
WritePng(outputPath, decoded.AtlasWidth, decoded.AtlasHeight, decoded.Rgba);

var manifest = new
{
    schema = "phlosion-za-ui-offscreen-probe-evidence-v1",
    source_file = Path.GetFileName(sourcePath),
    source_size = source.Length,
    source_sha256 = Convert.ToHexString(SHA256.HashData(source)).ToLowerInvariant(),
    packed_format = PackedFormat,
    packed_file = Path.GetFileName(outputPath),
    packed_sha256 = Convert.ToHexString(
        SHA256.HashData(File.ReadAllBytes(outputPath))).ToLowerInvariant(),
    source_format = "BNTX-0x1F05 / BC6H_UF16",
    cube_face_order = new[] { "+X", "-X", "+Y", "-Y", "+Z", "-Z" },
    cube_face_size = decoded.FaceSize,
    source_mip_count = decoded.MipCount,
    source_array_count = 6,
    decoded_width = decoded.AtlasWidth,
    decoded_height = decoded.AtlasHeight,
    decoded_channel_min = decoded.ChannelMin,
    decoded_channel_max = decoded.ChannelMax,
    decoded_channel_mean = decoded.ChannelMean,
    decoded_payload_sha256 = Convert.ToHexString(
        SHA256.HashData(decoded.CanonicalPayload)).ToLowerInvariant(),
};
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(manifestPath))!);
File.WriteAllText(
    manifestPath,
    JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }) + "\n");
return 0;

static Dictionary<string, string> ParseOptions(string[] values)
{
    if (values.Length % 2 != 0)
        throw new ArgumentException("Options must be --name value pairs.");
    var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    for (var index = 0; index < values.Length; index += 2)
    {
        if (!values[index].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException($"Expected option, got {values[index]}");
        result[values[index][2..]] = values[index + 1];
    }
    return result;
}

static string Required(Dictionary<string, string> values, string key) =>
    values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
        ? value
        : throw new ArgumentException($"Missing --{key}");

static DecodedProbe Decode(byte[] source)
{
    if (source.Length < 0x80 ||
        !source.AsSpan(0, 4).SequenceEqual("BNTX"u8) ||
        !source.AsSpan(0x20, 4).SequenceEqual("NX  "u8))
        throw new InvalidDataException("Not a supported Switch BNTX.");
    var textureCount = ReadUInt32(source, 0x24);
    var infoPointersOffset = ReadInt64(source, 0x28);
    if (textureCount != 1 || !InBounds(source, infoPointersOffset, 8))
        throw new InvalidDataException("Expected exactly one BNTX texture.");
    var brtiOffset = ReadInt64(source, checked((int)infoPointersOffset));
    if (!InBounds(source, brtiOffset, 0x78) ||
        !source.AsSpan(checked((int)brtiOffset), 4).SequenceEqual("BRTI"u8))
        throw new InvalidDataException("BNTX has no valid BRTI record.");

    var header = checked((int)brtiOffset);
    var dimensions = source[header + 0x11];
    var mipCount = ReadUInt16(source, header + 0x16);
    var format = ReadUInt32(source, header + 0x1c);
    var width = ReadInt32(source, header + 0x24);
    var height = ReadInt32(source, header + 0x28);
    var depth = ReadInt32(source, header + 0x2c);
    var arrayCount = ReadInt32(source, header + 0x30);
    var blockHeightLog2 = ReadInt32(source, header + 0x34);
    var dataLength = ReadInt32(source, header + 0x50);
    var channelTypes = ReadUInt32(source, header + 0x58);
    var textureType = ReadInt32(source, header + 0x5c);
    var pointersOffset = ReadInt64(source, header + 0x70);
    if (dimensions != 2 || mipCount is <= 0 or > 16 || format != 0x1f05 ||
        width <= 0 || width != height || (width & (width - 1)) != 0 ||
        depth != 1 || arrayCount != 6 || blockHeightLog2 is < 0 or > 5 ||
        channelTypes != 0x01040302 || textureType != 3 || dataLength <= 0 ||
        dataLength % arrayCount != 0 ||
        !InBounds(source, pointersOffset, checked(mipCount * 8L)))
        throw new InvalidDataException("Unsupported HDR cube topology.");

    var mipOffsets = new long[mipCount];
    for (var mip = 0; mip < mipCount; ++mip)
    {
        mipOffsets[mip] = ReadInt64(source, checked((int)pointersOffset + mip * 8));
        if (!InBounds(source, mipOffsets[mip], 1) ||
            (mip > 0 && mipOffsets[mip] <= mipOffsets[mip - 1]))
            throw new InvalidDataException("Invalid mip offsets.");
    }

    var atlasWidth = checked(width * 6);
    var atlasHeight = 0;
    for (var mip = 0; mip < mipCount; ++mip)
        atlasHeight += Math.Max(1, height >> mip) * 2;
    var rgba = new byte[checked(atlasWidth * atlasHeight * 4)];
    var canonical = new List<byte>();
    var channelMin = new[] { float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity };
    var channelMax = new[] { 0.0f, 0.0f, 0.0f };
    var channelSum = new[] { 0.0, 0.0, 0.0 };
    long pixelCount = 0;
    var decoder = new BcDecoder();
    var faceStride = dataLength / arrayCount;
    var mipY = 0;
    for (var mip = 0; mip < mipCount; ++mip)
    {
        var mipWidth = Math.Max(1, width >> mip);
        var mipHeight = Math.Max(1, height >> mip);
        var widthInBlocks = (mipWidth + 3) / 4;
        var heightInBlocks = (mipHeight + 3) / 4;
        var blockHeight = MipBlockHeight(heightInBlocks, 1 << blockHeightLog2);
        var swizzledMipSize = SwizzledSurfaceSize(
            widthInBlocks, heightInBlocks, 16, blockHeight);
        var linearBlockBytes = checked(widthInBlocks * heightInBlocks * 16);
        for (var face = 0; face < arrayCount; ++face)
        {
            var sourceFaceOffset = checked(mipOffsets[mip] + (long)face * faceStride);
            if (!InBounds(source, sourceFaceOffset, swizzledMipSize))
                throw new InvalidDataException("Mip face is out of bounds.");
            var linearBlocks = new byte[linearBlockBytes];
            for (var blockY = 0; blockY < heightInBlocks; ++blockY)
            for (var blockX = 0; blockX < widthInBlocks; ++blockX)
            {
                var swizzled = BlockLinearOffset(
                    blockX, blockY, widthInBlocks, 16, blockHeight);
                Buffer.BlockCopy(
                    source, checked((int)sourceFaceOffset + swizzled),
                    linearBlocks, (blockY * widthInBlocks + blockX) * 16, 16);
            }
            var pixels = decoder.DecodeRawHdr(
                linearBlocks, mipWidth, mipHeight, CompressionFormat.Bc6U);
            if (pixels.Length != checked(mipWidth * mipHeight))
                throw new InvalidDataException("BC6H decoder returned the wrong size.");
            var originX = (face % 3) * mipWidth * 2;
            var originY = mipY + (face / 3) * mipHeight;
            for (var y = 0; y < mipHeight; ++y)
            for (var x = 0; x < mipWidth; ++x)
            {
                var pixel = pixels[y * mipWidth + x];
                if (!float.IsFinite(pixel.r) || pixel.r < 0.0f ||
                    !float.IsFinite(pixel.g) || pixel.g < 0.0f ||
                    !float.IsFinite(pixel.b) || pixel.b < 0.0f)
                    throw new InvalidDataException("BC6H cube contains invalid HDR values.");
                var channels = new[] { pixel.r, pixel.g, pixel.b };
                for (var channel = 0; channel < channels.Length; ++channel)
                {
                    channelMin[channel] = Math.Min(channelMin[channel], channels[channel]);
                    channelMax[channel] = Math.Max(channelMax[channel], channels[channel]);
                    channelSum[channel] += channels[channel];
                }
                ++pixelCount;
                var words = new ushort[] {
                    BitConverter.HalfToUInt16Bits((System.Half)pixel.r),
                    BitConverter.HalfToUInt16Bits((System.Half)pixel.g),
                    BitConverter.HalfToUInt16Bits((System.Half)pixel.b),
                    0x3c00 };
                foreach (var word in words)
                {
                    canonical.Add((byte)word);
                    canonical.Add((byte)(word >> 8));
                }
                WriteCarrier(rgba, atlasWidth, originX + x * 2, originY + y,
                    (byte)words[0], (byte)(words[0] >> 8),
                    (byte)words[1], (byte)(words[1] >> 8));
                WriteCarrier(rgba, atlasWidth, originX + x * 2 + 1, originY + y,
                    (byte)words[2], (byte)(words[2] >> 8),
                    (byte)words[3], (byte)(words[3] >> 8));
            }
        }
        mipY += mipHeight * 2;
    }
    return new DecodedProbe(
        width, mipCount, atlasWidth, atlasHeight, rgba, canonical.ToArray(),
        channelMin, channelMax,
        channelSum.Select(value => value / pixelCount).ToArray());
}

static void WriteCarrier(
    byte[] destination, int width, int x, int y,
    byte red, byte green, byte blue, byte alpha)
{
    var offset = (y * width + x) * 4;
    destination[offset] = red;
    destination[offset + 1] = green;
    destination[offset + 2] = blue;
    destination[offset + 3] = alpha;
}

static void WritePng(string path, int width, int height, byte[] rgba)
{
    using var output = File.Create(path);
    output.Write(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 });
    Span<byte> ihdr = stackalloc byte[13];
    BinaryPrimitives.WriteUInt32BigEndian(ihdr, (uint)width);
    BinaryPrimitives.WriteUInt32BigEndian(ihdr[4..], (uint)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    WriteChunk(output, "IHDR"u8, ihdr);
    using var compressed = new MemoryStream();
    using (var zlib = new ZLibStream(compressed, CompressionLevel.SmallestSize, true))
    {
        for (var y = 0; y < height; ++y)
        {
            zlib.WriteByte(0);
            zlib.Write(rgba, y * width * 4, width * 4);
        }
    }
    WriteChunk(output, "IDAT"u8, compressed.ToArray());
    WriteChunk(output, "IEND"u8, ReadOnlySpan<byte>.Empty);
}

static void WriteChunk(Stream output, ReadOnlySpan<byte> kind, ReadOnlySpan<byte> data)
{
    Span<byte> length = stackalloc byte[4];
    BinaryPrimitives.WriteUInt32BigEndian(length, (uint)data.Length);
    output.Write(length);
    output.Write(kind);
    output.Write(data);
    var crc = Crc32(kind, data);
    BinaryPrimitives.WriteUInt32BigEndian(length, crc);
    output.Write(length);
}

static uint Crc32(ReadOnlySpan<byte> first, ReadOnlySpan<byte> second)
{
    var crc = 0xffffffffu;
    foreach (var span in new[] { first.ToArray(), second.ToArray() })
    foreach (var value in span)
    {
        crc ^= value;
        for (var bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint)-(int)(crc & 1));
    }
    return ~crc;
}

static int MipBlockHeight(int heightInBlocks, int baseBlockHeight)
{
    var result = baseBlockHeight;
    while (result > 1 && heightInBlocks <= result / 2 * 8) result /= 2;
    return result;
}

static int SwizzledSurfaceSize(int width, int height, int bpp, int blockHeight)
{
    var widthInGobs = (width * bpp + 63) / 64;
    var gobRows = (height + blockHeight * 8 - 1) / (blockHeight * 8);
    return checked(widthInGobs * gobRows * 512 * blockHeight);
}

static int BlockLinearOffset(int x, int y, int width, int bpp, int blockHeight)
{
    var bhMask = blockHeight * 8 - 1;
    var bhShift = TrailingZeroCount(blockHeight * 8);
    var bppShift = TrailingZeroCount(bpp);
    var widthInGobs = (width * bpp + 63) / 64;
    var byteX = x << bppShift;
    var position = (y >> bhShift) * 512 * blockHeight * widthInGobs;
    position += (byteX >> 6) << TrailingZeroCount(512 * blockHeight);
    position += ((y & bhMask) >> 3) << 9;
    position += ((byteX & 0x3f) >> 5) << 8;
    position += ((y & 7) >> 1) << 6;
    position += ((byteX & 0x1f) >> 4) << 5;
    position += (y & 1) << 4;
    position += byteX & 0xf;
    return position;
}

static int TrailingZeroCount(int value)
{
    var count = 0;
    while (((value >> count) & 1) == 0) ++count;
    return count;
}

static bool InBounds(byte[] source, long offset, long length) =>
    offset >= 0 && length >= 0 && offset <= source.LongLength - length;
static ushort ReadUInt16(byte[] source, int offset) =>
    BinaryPrimitives.ReadUInt16LittleEndian(source.AsSpan(offset, 2));
static uint ReadUInt32(byte[] source, int offset) =>
    BinaryPrimitives.ReadUInt32LittleEndian(source.AsSpan(offset, 4));
static int ReadInt32(byte[] source, int offset) =>
    BinaryPrimitives.ReadInt32LittleEndian(source.AsSpan(offset, 4));
static long ReadInt64(byte[] source, int offset) =>
    BinaryPrimitives.ReadInt64LittleEndian(source.AsSpan(offset, 8));

internal sealed record DecodedProbe(
    int FaceSize,
    int MipCount,
    int AtlasWidth,
    int AtlasHeight,
    byte[] Rgba,
    byte[] CanonicalPayload,
    float[] ChannelMin,
    float[] ChannelMax,
    double[] ChannelMean);
