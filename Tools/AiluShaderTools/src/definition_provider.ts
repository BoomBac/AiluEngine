import * as vscode from "vscode";
import { fieldAt, fieldSchema, ShaderMetaDocument } from "./parser";
import { ShaderMetaSchema } from "./schema";

export class ShaderDefinitionProvider implements vscode.DefinitionProvider {
    public constructor(private readonly _schema: ShaderMetaSchema, private readonly _getData: (document: vscode.TextDocument) => ShaderMetaDocument) {}

    public provideDefinition(document: vscode.TextDocument, position: vscode.Position): vscode.Location[] {
        const data = this._getData(document);
        const field = fieldAt(data, position);
        if (!field)
            return [];
        const schema_entry = fieldSchema(this._schema, field);
        if (!schema_entry || schema_entry[1].type !== "shader_entry" || !field.value_range.contains(position))
            return [];
        const symbol = data.symbols.find(item => item.name === field.value.trim());
        return symbol ? [new vscode.Location(document.uri, symbol.range)] : [];
    }
}
