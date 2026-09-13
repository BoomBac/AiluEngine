import * as vscode from "vscode";
import { fieldAt, fieldSchema, passAt, ShaderMetaDocument } from "./parser";
import { findField, ShaderMetaSchema } from "./schema";

function valueDescription(key: string, value: string): string {
    if (key.toLowerCase() === "ztest")
        return value === "LEqual" ? "Depth comparison: Less or Equal.\n\nAiluEngine may map this through its reversed-Z configuration." : "Depth comparison function.";
    if (key.toLowerCase() === "queue" && value.toLowerCase() === "opaque")
        return "Opaque render queue classification. Default queue: 2000.";
    if (key.toLowerCase() === "blend")
        return "Enables blending using the two factors written as source,destination.";
    if (key.toLowerCase() === "multi_compile")
        return "A keyword group that creates shader variants for the current pass.";
    return "";
}

export class ShaderHoverProvider implements vscode.HoverProvider {
    public constructor(private readonly _schema: ShaderMetaSchema, private readonly _getData: (document: vscode.TextDocument) => ShaderMetaDocument) {}

    public provideHover(document: vscode.TextDocument, position: vscode.Position): vscode.Hover | undefined {
        const data = this._getData(document);
        const field = fieldAt(data, position);
        if (!field || !passAt(data, position.line))
            return undefined;
        const schema_entry = fieldSchema(this._schema, field);
        if (!schema_entry)
            return undefined;
        const [key, field_info] = schema_entry;
        const markdown = new vscode.MarkdownString();
        markdown.appendCodeblock(`//${key}: ${field.value}`, "hlsl");
        markdown.appendMarkdown(field_info.description ?? "Ailu shader metadata field.");
        const value_help = valueDescription(key, field.value);
        if (value_help)
            markdown.appendMarkdown(`\n\n${value_help}`);
        if (field_info.values?.length)
            markdown.appendMarkdown(`\n\nAllowed values: \`${field_info.values.join("` | `")}\``);
        if (field_info.aliases && Object.keys(field_info.aliases).length)
            markdown.appendMarkdown(`\n\nLegacy aliases: ${Object.keys(field_info.aliases).map(alias => `\`${alias}\``).join(", ")}`);
        return new vscode.Hover(markdown, field.key_range.union(field.value_range));
    }
}
