# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

| Item | Why it Matters | Done When |
| --- | --- | --- |
| Release packaging is manual | Hard to produce consistent builds | One scripted or documented release bundle path |
| Local dev loop skips content validation | Bad data may only be caught late | One command runs build + tests + `PAC_ValidateData` |
