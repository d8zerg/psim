const rawColor =
  /#[\da-f]{3,8}\b|\b(?:rgb|rgba|hsl|hsla|oklch|oklab|lab|lch)\s*\(|\b(?:bg|text|border|ring|fill|stroke)-(?:red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose|slate|gray|zinc|neutral|stone)-(?:50|[1-9]00)\b/iu;
export default {
  meta: {
    type: "problem",
    schema: [],
    messages: { token: "Use semantic design tokens instead of literal colors or palette utilities." },
  },
  create(context) {
    return {
      Literal(node) {
        if (typeof node.value === "string" && rawColor.test(node.value)) context.report({ node, messageId: "token" });
      },
      TemplateElement(node) {
        if (rawColor.test(node.value.raw)) context.report({ node, messageId: "token" });
      },
    };
  },
};
