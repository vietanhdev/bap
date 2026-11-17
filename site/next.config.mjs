import nextra from 'nextra'

const withNextra = nextra({
  defaultShowCopyCode: true,
  search: { codeblocks: false },
  unstable_shouldAddLocaleToLinks: true
})

export default withNextra({
  reactStrictMode: true,
  i18n: {
    locales: ['en', 'vi', 'ko', 'zh'],
    defaultLocale: 'en'
  },
  images: { unoptimized: true }
})
