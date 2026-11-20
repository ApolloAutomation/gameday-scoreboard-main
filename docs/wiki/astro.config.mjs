// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
	site: 'https://docs.gameday-scoreboard.com',
	integrations: [
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
				{ tag: 'meta', attrs: { property: 'og:image', content: 'https://docs.gameday-scoreboard.com/og.jpg' } },
				{ tag: 'meta', attrs: { name: 'twitter:card', content: 'summary_large_image' } },
				{ tag: 'meta', attrs: { name: 'twitter:site', content: '@GameDayScore' } },
			],
			customCss: ['./src/styles/custom.css'],
			sidebar: [
				{ label: 'Get started', items: [{ autogenerate: { directory: 'get-started' } }] },
				{ label: 'Using your panel', items: [{ autogenerate: { directory: 'using' } }] },
				{ label: 'Help', items: [{ autogenerate: { directory: 'help' } }] },
				{ label: 'DIY', items: [{ autogenerate: { directory: 'diy' } }] },
				{ label: "What's new", link: '/changelog/' },
			],
		}),
	],
});
