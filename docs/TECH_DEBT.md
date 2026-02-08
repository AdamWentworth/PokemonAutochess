# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

| Item | Why it Matters | Done When |
| --- | --- | --- |
| UI sizing uses magic numbers | Makes UI brittle across resolutions | UI dimensions come from a single viewport service |
| Content validation is not in every dev loop | Bad data may only be caught late | CI always runs `PAC_ValidateData` |
