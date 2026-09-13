# Ailu Shader Tools

VS Code support for AiluEngine's HLSL metadata comments. The extension attaches
to the existing `hlsl` language and does not replace an HLSL IntelliSense
extension.

It provides:

- Schema-driven pass key and enum value completion;
- Blend and Stencil nested completion;
- Current-file shader entry completion and F12 navigation;
- Hover documentation for metadata;
- Per-field semantic highlighting for pass metadata and Properties types/annotations;
- Structural, key, value, duplicate-field and keyword diagnostics;
- `ailupass` and `ailuinfo` snippets;
- Commands to validate, normalize legacy aliases, and insert a pass.

Build and test from this directory:

```text
npm install
npm test
npm run package
```

The schema is stored in `schema/shader_meta_schema.json`. Legacy `//info bein`,
`Nevel`, and Cull `none` aliases are accepted and reported as hints; generated
completion uses the canonical spelling.
