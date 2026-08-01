const vscode = require("vscode");

/**
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
    const provider = new JsonColorProvider();
    context.subscriptions.push(
        vscode.languages.registerColorProvider(
            [
                { language: "json" },
                { language: "jsonc" },
                { pattern: "**/UITheme_Dark.json" },
                { pattern: "**/UITheme_Light.json" },
                { pattern: "**/EditorStyle.json" }
            ],
            provider
        )
    );
}

class JsonColorProvider {
    /**
     * @param {vscode.TextDocument} document
     * @param {vscode.CancellationToken} token
     * @returns {vscode.ProviderResult<vscode.ColorInformation[]>}
     */
    provideDocumentColors(document, token) {
        /** @type {vscode.ColorInformation[]} */
        const colors = [];
        const text = document.getText();

        // Regex: matches JSON keys containing "color" followed by a 4-element array
        // e.g. "_window_title_bar_color": [0.118, 0.726, 0.145, 1.0]
        const regex = /"([^"]*color[^"]*)"\s*:\s*\[\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\]/gi;

        let match;
        while ((match = regex.exec(text)) !== null) {
            const [, , rStr, gStr, bStr, aStr] = match;
            const r = parseFloat(rStr);
            const g = parseFloat(gStr);
            const b = parseFloat(bStr);
            const a = parseFloat(aStr);

            if (isNaN(r) || isNaN(g) || isNaN(b) || isNaN(a)) continue;

            // Skip if any component is outside [0,1] — likely not a color
            if (r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1 || a < 0 || a > 1) continue;

            // Range must cover only the array portion [x,x,x,x], not the key.
            // Otherwise VSCode replaces the key+colon as well when editing.
            const fullMatch = match[0];
            const arrayStart = fullMatch.indexOf('[');
            const startPos = document.positionAt(match.index + arrayStart);
            const endPos = document.positionAt(match.index + fullMatch.length);
            const range = new vscode.Range(startPos, endPos);

            const color = new vscode.Color(r, g, b, a);
            colors.push(new vscode.ColorInformation(range, color));
        }

        return colors;
    }

    /**
     * @param {vscode.Color} color
     * @param {{ document: vscode.TextDocument; range: vscode.Range }} context
     * @param {vscode.CancellationToken} token
     * @returns {vscode.ProviderResult<vscode.ColorPresentation[]>}
     */
    provideColorPresentations(color, context, token) {
        const r = color.red.toFixed(3);
        const g = color.green.toFixed(3);
        const b = color.blue.toFixed(3);
        const a = color.alpha.toFixed(3);

        return [
            new vscode.ColorPresentation(`[${r}, ${g}, ${b}, ${a}]`)
        ];
    }
}

function deactivate() {}

module.exports = { activate, deactivate };
