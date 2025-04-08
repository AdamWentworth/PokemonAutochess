-- scripts/states/starter_selection.lua

function onCardClick(pokemon)
    print("Lua: selected " .. pokemon)
    spawnPokemon(pokemon, 0.0, 0.0, 3.5)
end
