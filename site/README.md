# Bắp documentation site

The multilingual documentation site for Bắp, served at <https://bap.nrl.ai>.

- **Stack:** Next.js 15 (App Router) + [Nextra 4](https://nextra.site) docs theme, Pagefind search.
- **Languages:** English (`en`, default), Vietnamese (`vi`), Korean (`ko`), Chinese (`zh`). Visitors
  are redirected from `/` to their browser language.
- **Content:** `content/<locale>/*.mdx`, one folder per language with the same slugs. Sidebar order
  and titles are in each `content/<locale>/_meta.js`; the slug list for the sitemap is `lib/pages.js`.
- **Landing page:** `components/Landing.jsx`, strings in `lib/i18n.js`.
- **Images:** `public/screens` (device screenshots), `public/diagrams` (SVGs from `docs/images`),
  `public/web` (settings page with mock data), `public/faces`.

```bash
pnpm install
pnpm dev          # http://localhost:3000
pnpm build        # also builds the Pagefind search index
```

Adding a page: create `content/en/<slug>.mdx` and the same slug in `vi`, `ko` and `zh`, add it to
each `_meta.js`, and to `lib/pages.js`.

Deploy (Vercel project `bap-docs`, team `nrl-ai`):

```bash
vercel deploy --prod --scope nrl-ai
```

Note: `zod` is pinned to 4.1.13 with a pnpm override; Nextra 4.6.1's layout schema breaks with zod
4.6.
