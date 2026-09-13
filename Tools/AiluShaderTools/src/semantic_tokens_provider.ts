import * as vscode from "vscode";
import { fieldSchema, ShaderMetaDocument } from "./parser";
import { findField, ShaderFieldSchema, ShaderMetaSchema } from "./schema";

const kTokenTypes = [
    "ailuShaderName",
    "ailuShaderVert",
    "ailuShaderPixel",
    "ailuShaderGeometry",
    "ailuShaderCull",
    "ailuShaderQueue",
    "ailuShaderTopology",
    "ailuShaderBlend",
    "ailuShaderFill",
    "ailuShaderZTest",
    "ailuShaderZWrite",
    "ailuShaderStencil",
    "ailuShaderColorMask",
    "ailuShaderConservative",
    "ailuShaderMultiCompile",
    "ailuShaderPropertyType",
    "ailuShaderPropertyAnnotation",
    "ailuShaderUnknown"
];

export const kSemanticTokensLegend = new vscode.SemanticTokensLegend(kTokenTypes);

function tokenTypeForField(key: string, field: ShaderFieldSchema | undefined): string {
    if (!field)
        return "ailuShaderUnknown";
    const normalized_key = key.toLowerCase();
    if (normalized_key === "multi_compile")
        return "ailuShaderMultiCompile";
    if (normalized_key === "name")
        return "ailuShaderName";
    if (normalized_key === "vert")
        return "ailuShaderVert";
    if (normalized_key === "pixel")
        return "ailuShaderPixel";
    if (normalized_key === "geometry")
        return "ailuShaderGeometry";
    if (normalized_key === "cull")
        return "ailuShaderCull";
    if (normalized_key === "queue")
        return "ailuShaderQueue";
    if (normalized_key === "topology")
        return "ailuShaderTopology";
    if (normalized_key === "blend")
        return "ailuShaderBlend";
    if (normalized_key === "fill")
        return "ailuShaderFill";
    if (normalized_key === "ztest")
        return "ailuShaderZTest";
    if (normalized_key === "zwrite")
        return "ailuShaderZWrite";
    if (normalized_key === "stencil")
        return "ailuShaderStencil";
    if (normalized_key === "colormask")
        return "ailuShaderColorMask";
    if (normalized_key === "conservative")
        return "ailuShaderConservative";
    return "ailuShaderUnknown";
}

export class ShaderSemanticTokensProvider implements vscode.DocumentSemanticTokensProvider {
    public constructor(private readonly _schema: ShaderMetaSchema,
                       private readonly _getData: (document: vscode.TextDocument) => ShaderMetaDocument) {}

    public provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.SemanticTokens {
        const builder = new vscode.SemanticTokensBuilder(kSemanticTokensLegend);
        const data = this._getData(document);
        for (const field of data.fields) {
            const schema_entry = fieldSchema(this._schema, field);
            const token_type = tokenTypeForField(field.key, schema_entry?.[1]);
            builder.push(field.key_range, token_type);
            if (!field.value_range.isEmpty)
                builder.push(field.value_range, token_type);
        }
        this.pushPropertyTokens(document, builder);
        return builder.build();
    }

    private pushPropertyTokens(document: vscode.TextDocument, builder: vscode.SemanticTokensBuilder): void {
        let active = false;
        for (let line_number = 0; line_number < document.lineCount; ++line_number) {
            const line = document.lineAt(line_number).text;
            const trimmed = line.trim().toLowerCase();
            if (trimmed === "//properties") {
                active = true;
                continue;
            }
            if (!active)
                continue;
            if (trimmed === "//}") {
                active = false;
                continue;
            }
            const annotation = /^(\s*\/\/\s*)\[([^\]]+)\]/.exec(line);
            if (annotation) {
                const start = annotation[1].length;
                builder.push(new vscode.Range(new vscode.Position(line_number, start),
                    new vscode.Position(line_number, start + annotation[2].length + 2)), "ailuShaderPropertyAnnotation");
            }
            const type_start = line.indexOf(",", annotation?.index ?? 0);
            if (type_start < 0)
                continue;
            const type_match = /\b(Texture2D|Texture3D|Color|Range|Float|Vector)\b/.exec(line.slice(type_start + 1));
            if (!type_match)
                continue;
            const start = type_start + 1 + type_match.index;
            builder.push(new vscode.Range(new vscode.Position(line_number, start),
                new vscode.Position(line_number, start + type_match[0].length)), "ailuShaderPropertyType");
        }
    }
}
