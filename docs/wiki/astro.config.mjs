// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// The project's site; the docs live under its /docs/.
const SITE_URL = (process.env.SITE_URL || 'https://apolloautomation.github.io/gameday-scoreboard-main').replace(/\/+$/, '');
const site = new URL(SITE_URL);
const base = `${site.pathname.replace(/\/+$/, '')}/docs`;
const DOCS_URL = `${site.origin}${base}`;

// Page links written from the docs root get the base.
function withBase(href) {
	if (typeof href !== 'string' || !href.startsWith('/') || href.startsWith('//')) return href;
	if (href === base || href.startsWith(`${base}/`)) return href;
	return base + href;
}

const baseLinks = {
	name: 'docs-base-links',
	element: {
		filter: ['a', 'img'],
		visit(node, ctx) {
			for (const key of ['href', 'src']) {
				const value = node.properties?.[key];
				const next = withBase(value);
				if (next !== value) ctx.setProperty(node, key, next);
			}
		},
	},
};

// Adds baseLinks to Astro's Markdown processor.
const docsBaseLinks = {
	name: 'docs-base-links',
	hooks: {
		'astro:config:setup': ({ config }) => {
			const plugins = config.markdown.processor?.options?.hastPlugins;
			if (!plugins) throw new Error('docs-base-links: Markdown processor takes no hast plugins');
			plugins.push(baseLinks);
		},
	},
};

export default defineConfig({
	site: site.origin,
	base,
	integrations: [
		docsBaseLinks,
		starlight({
			title: 'Apollo Pulse',
			description:
				"How to set up, use and build Apollo Pulse, an LED panel that follows your team's games live and can show the weather, the planes overhead, your stocks and coins, or Formula 1.",
			logo: { src: './src/assets/logo.svg', replacesTitle: false },
			favicon: '/favicon.svg',
			social: [
				{
					icon: 'github',
					label: 'GitHub',
					href: 'https://github.com/ApolloAutomation/gameday-scoreboard-main',
				},
			],
			editLink: {
				baseUrl: 'https://github.com/ApolloAutomation/gameday-scoreboard-main/edit/dev/docs/wiki/',
			},
			lastUpdated: true,
			head: [
				{ tag: 'meta', attrs: { property: 'og:site_name', content: 'Apollo Pulse' } },
				{ tag: 'meta', attrs: { property: 'og:type', content: 'website' } },
				{ tag: 'meta', attrs: { property: 'og:image', content: `${DOCS_URL}/og.jpg` } },
				{ tag: 'meta', attrs: { name: 'twitter:card', content: 'summary_large_image' } },
				{ tag: 'meta', attrs: { name: 'twitter:image', content: `${DOCS_URL}/og.jpg` } },
				{ tag: 'meta', attrs: { name: 'twitter:site', content: '@GameDayScore' } },
			],
			customCss: ['./src/styles/custom.css'],
			sidebar: [
				{ label: 'Get started', items: [{ autogenerate: { directory: 'get-started' } }] },
				{ label: 'Using your panel', items: [{ autogenerate: { directory: 'using' } }] },
				{ label: 'Help', items: [{ autogenerate: { directory: 'help' } }] },
				{ label: 'DIY', items: [{ autogenerate: { directory: 'diy' } }] },
				{ label: "What's new", slug: 'changelog' },
			],
		}),
	],
});
