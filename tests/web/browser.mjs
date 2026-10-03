// Drives a real browser (Edge or Chrome, headless) through the DevTools protocol, with no
// extra packages: Node's own fetch and WebSocket. Used to test FeedView's web page itself.
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

/** A Chromium-based browser on this computer (FEEDVIEW_BROWSER overrides), or null. */
export function findBrowser() {
	const env = process.env.FEEDVIEW_BROWSER
	if (env) return fs.existsSync(env) ? env : null
	const pf = process.env['ProgramFiles'] || 'C:\\Program Files'
	const pf86 = process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)'
	const local = process.env.LOCALAPPDATA || ''
	const candidates = [
		path.join(pf86, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
		path.join(pf, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
		path.join(pf, 'Google', 'Chrome', 'Application', 'chrome.exe'),
		path.join(pf86, 'Google', 'Chrome', 'Application', 'chrome.exe'),
		path.join(local, 'Google', 'Chrome', 'Application', 'chrome.exe'),
		'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
		'/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
		'/usr/bin/google-chrome',
		'/usr/bin/chromium',
		'/usr/bin/chromium-browser',
		'/usr/bin/microsoft-edge',
	]
	return candidates.find((c) => c && fs.existsSync(c)) || null
}

async function waitFor(fn, ms, what) {
	const end = Date.now() + ms
	for (;;) {
		const v = await fn()
		if (v) return v
		if (Date.now() > end) throw new Error(`Timed out waiting for ${what}`)
		await new Promise((r) => setTimeout(r, 50))
	}
}

export class Page {
	#ws
	#id = 0
	#pending = new Map()
	#listeners = new Map()
	/** Every request the page sent: {method, url, path, body} */
	requests = []

	static async open(browserExe) {
		const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'feedview-browser-'))
		const proc = spawn(
			browserExe,
			[
				'--headless=new',
				'--remote-debugging-port=0',
				`--user-data-dir=${dir}`,
				'--no-first-run',
				'--no-default-browser-check',
				'--disable-gpu',
				'--window-size=1280,1000',
				'about:blank',
			],
			{ stdio: 'ignore' },
		)
		const portFile = path.join(dir, 'DevToolsActivePort')
		const port = await waitFor(
			() => fs.existsSync(portFile) && fs.readFileSync(portFile, 'utf8').split('\n')[0].trim(),
			20000,
			'the browser to start',
		)
		const targets = await waitFor(
			async () => {
				const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json().catch(() => [])
				return list.find((t) => t.type === 'page')
			},
			10000,
			'a browser tab',
		)
		const page = new Page()
		page.proc = proc
		page.dir = dir
		await page.#connect(targets.webSocketDebuggerUrl)
		await page.send('Page.enable')
		await page.send('Runtime.enable')
		await page.send('Network.enable')
		page.on('Network.requestWillBeSent', (p) => {
			const u = new URL(p.request.url)
			page.requests.push({ method: p.request.method, url: p.request.url, path: u.pathname, body: p.request.postData || '' })
		})
		return page
	}

	async #connect(url) {
		this.#ws = new WebSocket(url)
		this.#ws.onmessage = (ev) => {
			const msg = JSON.parse(ev.data)
			if (msg.id && this.#pending.has(msg.id)) {
				const { resolve, reject } = this.#pending.get(msg.id)
				this.#pending.delete(msg.id)
				if (msg.error) reject(new Error(msg.error.message))
				else resolve(msg.result)
			} else if (msg.method) {
				for (const fn of this.#listeners.get(msg.method) || []) fn(msg.params)
			}
		}
		await new Promise((resolve, reject) => {
			this.#ws.onopen = resolve
			this.#ws.onerror = () => reject(new Error('DevTools connection failed'))
		})
	}

	send(method, params = {}) {
		const id = ++this.#id
		return new Promise((resolve, reject) => {
			this.#pending.set(id, { resolve, reject })
			this.#ws.send(JSON.stringify({ id, method, params }))
		})
	}

	on(method, fn) {
		if (!this.#listeners.has(method)) this.#listeners.set(method, [])
		this.#listeners.get(method).push(fn)
	}

	async goto(url) {
		const loaded = new Promise((r) => this.on('Page.loadEventFired', r))
		await this.send('Page.navigate', { url })
		await loaded
	}

	/** Runs JavaScript in the page and returns its (JSON) result. */
	async eval(expression) {
		const r = await this.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })
		if (r.exceptionDetails) throw new Error(`In the page: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`)
		return r.result.value
	}

	/** Waits until a JavaScript expression in the page is truthy; returns its value. */
	waitFor(expression, ms = 5000, what = expression) {
		return waitFor(() => this.eval(expression), ms, what)
	}

	/** Clicks the first element matching a CSS selector, as a click event (like a tap). */
	async click(selector) {
		const ok = await this.eval(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); if (!e || e.disabled) return false; e.click(); return true })()`)
		if (!ok) throw new Error(`Nothing to click at ${selector} (missing or disabled)`)
	}

	/** The last request the page sent to an /api path, waiting up to `ms` for one after `since`. */
	async request(apiPath, since = 0, ms = 3000) {
		return waitFor(
			() => this.requests.slice(since).reverse().find((r) => r.path === apiPath),
			ms,
			`the page to send ${apiPath}`,
		)
	}

	async close() {
		try {
			await this.send('Browser.close')
		} catch {
			// already gone
		}
		this.proc.kill()
		await new Promise((r) => setTimeout(r, 300))
		fs.rmSync(this.dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 })
	}
}
