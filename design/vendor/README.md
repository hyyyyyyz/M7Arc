# Prototype dependencies

## Lucide 0.468.0

- File: `lucide-0.468.0.min.js` (357,796 bytes)
- Package: [`lucide@0.468.0`](https://www.npmjs.com/package/lucide/v/0.468.0)
- Source archive: [`lucide-0.468.0.tgz`](https://registry.npmjs.org/lucide/-/lucide-0.468.0.tgz)
- Archive member: `package/dist/umd/lucide.min.js`, preserved without modification.
- Upstream repository: [lucide-icons/lucide](https://github.com/lucide-icons/lucide)
- License: ISC; the complete upstream license is in [`lucide-LICENSE.txt`](lucide-LICENSE.txt), including the Feather attribution.

The standalone prototype embeds this distribution to work offline. The inline prototype uses the host's Lucide runtime. Both use Lucide's original icon data.

The UMD distribution exposes `window.lucide`. The prototype uses the standard Lucide attribute, matching the inline host:

```html
<i data-lucide="archive" aria-hidden="true"></i>
```

```javascript
lucide.createIcons({
  attrs: { width: 16, height: 16, 'stroke-width': 1.7 }
});
```

Call `createIcons` after inserting new placeholders. Set icon color with CSS `color`; Lucide uses `currentColor`. In version 0.468.0, `createIcons` accepts `icons`, `nameAttr`, and `attrs`; it does not accept a root element.

API reference: [Lucide for Vanilla JavaScript](https://lucide.dev/guide/lucide). The bundled version's implementation remains the reference for its exact API.
