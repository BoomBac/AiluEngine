import * as vscode from "vscode";
import { parseShaderDocument, ShaderMetaDocument } from "./parser";
import { ShaderMetaSchema } from "./schema";

export class DocumentIndex {
    private readonly _documents = new Map<string, ShaderMetaDocument>();
    private readonly _timers = new Map<string, ReturnType<typeof setTimeout>>();

    public constructor(private readonly _schema: ShaderMetaSchema) {}

    public update(document: vscode.TextDocument): ShaderMetaDocument {
        const parsed = parseShaderDocument(document, this._schema);
        this._documents.set(document.uri.toString(), parsed);
        return parsed;
    }

    public scheduleUpdate(document: vscode.TextDocument, callback: (data: ShaderMetaDocument) => void): void {
        const key = document.uri.toString();
        const old_timer = this._timers.get(key);
        if (old_timer)
            clearTimeout(old_timer);
        this._timers.set(key, setTimeout(() => {
            this._timers.delete(key);
            callback(this.update(document));
        }, 150));
    }

    public get(document: vscode.TextDocument): ShaderMetaDocument {
        return this._documents.get(document.uri.toString()) ?? this.update(document);
    }

    public remove(document: vscode.TextDocument): void {
        const key = document.uri.toString();
        const timer = this._timers.get(key);
        if (timer)
            clearTimeout(timer);
        this._timers.delete(key);
        this._documents.delete(key);
    }
}
