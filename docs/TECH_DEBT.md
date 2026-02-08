# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

| Item | Why it Matters | Done When |
| --- | --- | --- |
| Script boundary is still wide | Lua can reach internal objects, which blocks refactors | Lua only talks to ScriptAPI value types |
| ECS adoption incomplete | Core gameplay systems still split across styles | Movement, combat, and round logic run as ECS systems |
| UI sizing uses magic numbers | Makes UI brittle across resolutions | UI dimensions come from a single viewport service |
| Render regressions not tested | Visual setup breaks without detection | A render asset load smoke test exists |
| Movement regressions not tested | Pathing bugs slip into builds | A deterministic movement invariants test exists |
| Layering enforcement is manual | Accidental engine/game coupling can creep in | Build or lint checks enforce forbidden includes |
| Content validation is not in every dev loop | Bad data may only be caught late | CI always runs `PAC_ValidateData` |
