# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

| Item | Why it Matters | Done When |
| --- | --- | --- |
| ECS adoption incomplete | Core gameplay systems still split across styles | Movement, combat, and round logic run as ECS systems |
| UI sizing uses magic numbers | Makes UI brittle across resolutions | UI dimensions come from a single viewport service |
| Animset validation incomplete | Role coverage exists, but full clip mappings can drift | All animset clips validated against GLB animation names |
| Layering enforcement is manual | Accidental engine/game coupling can creep in | Build or lint checks enforce forbidden includes |
| Content validation is not in every dev loop | Bad data may only be caught late | CI always runs `PAC_ValidateData` |
