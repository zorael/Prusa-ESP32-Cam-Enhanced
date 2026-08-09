/** @type {import('tailwindcss').Config} */
// Tailwind v3 deliberately: the palette below is v3 `theme.extend` syntax, and it is
// what the page was written against. v4 moves theming into CSS and renames enough
// utilities that a bump needs the UI re-checked, not just a version change.
module.exports = {
  // Tailwind scans this file as plain text, so class names inside the inline
  // <script> are picked up too — including the ones applied via classList and
  // className. Every one of those is a complete literal in the source. If you ever
  // build a class by concatenation ('bg-' + colour), Tailwind cannot see it and you
  // must add it to `safelist` or it will silently not exist at runtime.
  content: ['./index.html'],
  theme: {
    extend: {
      colors: {
        base: '#1e1e1e',
        surface: '#2a2a2a',
        panel: '#333333',
        border: '#444444',
        muted: '#797979',
        text: '#e0e0e0',
        sub: '#a0a0a0',
        orange: '#FA6831',
        'orange-dim': '#d4561f',
        success: '#4caf50',
        warn: '#ff9800',
        danger: '#f44336',
      },
    },
  },
};
