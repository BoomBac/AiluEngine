import * as fs from "node:fs";
import * as path from "node:path";

export type ShaderFieldType = "string" | "integer" | "enum" | "blend" | "shader_entry" | "stencil" | "color_mask" | "keywords";

export interface ShaderFieldSchema {
    type: ShaderFieldType;
    stage?: "vertex" | "pixel" | "geometry";
    required?: boolean;
    values?: string[];
    aliases?: Record<string, string>;
    children?: Record<string, ShaderFieldSchema>;
    description?: string;
}

export interface ShaderPropertySchema {
    description: string;
    insert_text?: string;
}

export interface ShaderMetaSchema {
    version: number;
    markers: {
        info_begin: string;
        info_begin_aliases: string[];
        info_end: string;
        pass_begin: string;
        pass_end: string;
    };
    fields: Record<string, ShaderFieldSchema>;
    properties: {
        types: Record<string, ShaderPropertySchema>;
        annotations: Record<string, ShaderPropertySchema>;
    };
}

export function loadSchema(extension_path: string): ShaderMetaSchema {
    const schema_path = path.join(extension_path, "schema", "shader_meta_schema.json");
    return JSON.parse(fs.readFileSync(schema_path, "utf8")) as ShaderMetaSchema;
}

export function findField(schema: ShaderMetaSchema, key: string): [string, ShaderFieldSchema] | undefined {
    const entry = Object.entries(schema.fields).find(([name]) => name.toLowerCase() === key.toLowerCase());
    return entry;
}

export function canonicalValue(field: ShaderFieldSchema, value: string): string | undefined {
    if (field.values?.some(item => item.toLowerCase() === value.toLowerCase()))
        return field.values.find(item => item.toLowerCase() === value.toLowerCase());
    const alias = Object.entries(field.aliases ?? {}).find(([name]) => name.toLowerCase() === value.toLowerCase());
    return alias?.[1];
}
