const { defaultTheme } = require('vuepress')
const { searchPlugin } = require('@vuepress/plugin-search')

module.exports = {
  lang: 'en-US',
  title: 'rtl_433',
  description: 'generic data receiver for ISM/SRD bands.',

  base: '/rtl_433/',
  markdown: {
    code: {
      lineNumbers: false,
    },
  },

  plugins: [
    searchPlugin(),
  ],

  theme: defaultTheme({
    repo: 'merbanan/rtl_433',
    displayAllHeaders: true,

    editLink: true,
    docsBranch: 'master',
    docsDir: 'docs',

    navbar: [
      { text: 'Projects', link: 'https://triq.org/' },
    ],

    sidebar: [
      { text: 'Overview', link: '/' },
      'BUILDING',
      'STARTING',
      'CHANGELOG',
      'CONTRIBUTING',
      'PRIMER',
      'IQ_FORMATS',
      'ANALYZE',
      'OPERATION',
      'DATA_FORMAT',
      'HARDWARE',
      'INTEGRATION',
      'LINKS',
      'TESTS',
    ],
  }),
};
