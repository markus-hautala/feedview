// FeedView's web remote API (the one its own web page uses): GET /api/ping, GET /api/state,
// POST /api/<command> with form parameters. See FeedView's README, "HTTP API".

class ApiError extends Error {
	/**
	 * @param {number} status HTTP status, 0 = FeedView couldn't be reached
	 * @param {string} message
	 */
	constructor(status, message) {
		super(message)
		this.status = status
	}
}

class FeedViewClient {
	/**
	 * @param {{host: string, port: number, pin: string, timeoutMs?: number}} options
	 */
	constructor({ host, port, pin, timeoutMs = 3000 }) {
		this.host = host
		this.port = port
		this.pin = pin || ''
		this.timeoutMs = timeoutMs
		// IPv6 addresses go in brackets in a URL.
		this.base = `http://${host.includes(':') ? `[${host}]` : host}:${port}`
	}

	/** App name, version, host and whether a PIN is needed. Never needs a PIN, so it never counts as a wrong one. */
	async ping() {
		const res = await this.#fetch('/api/ping', {})
		const body = await res.json().catch(() => null)
		if (!res.ok || !body || body.app !== 'FeedView')
			throw new ApiError(res.status, `No FeedView at ${this.host}:${this.port}`)
		return body
	}

	/** Everything the web page shows. */
	async state() {
		const res = await this.#fetch('/api/state', { headers: this.#headers() })
		const body = await res.json().catch(() => null)
		if (!res.ok) throw new ApiError(res.status, (body && body.message) || `FeedView answered ${res.status}`)
		if (!body || !body.app) throw new ApiError(res.status, 'FeedView sent something unexpected')
		return body
	}

	/**
	 * Runs a command. Resolves with FeedView's answer ({ok, message, state}); a refused
	 * command (unknown source, wrong value...) resolves with ok false. Throws ApiError for
	 * authentication problems and when FeedView can't be reached.
	 * @param {string} action e.g. "source", "fullscreen"
	 * @param {Record<string, string|number>} params
	 */
	async command(action, params = {}) {
		const body = new URLSearchParams()
		for (const [k, v] of Object.entries(params)) body.set(k, String(v))
		const res = await this.#fetch(`/api/${action}`, {
			method: 'POST',
			headers: { ...this.#headers(), 'Content-Type': 'application/x-www-form-urlencoded' },
			body: body.toString(),
		})
		const json = await res.json().catch(() => ({}))
		if (res.status === 401 || res.status === 403 || res.status === 429)
			throw new ApiError(res.status, json.message || `FeedView answered ${res.status}`)
		return {
			ok: res.ok && json.ok !== false,
			status: res.status,
			message: json.message || (res.ok ? '' : `FeedView answered ${res.status}`),
			state: json.state,
		}
	}

	// Commands need this header (it stops other web pages from sending commands); its value
	// is the PIN, or anything when FeedView doesn't require one.
	#headers() {
		return { 'X-FeedView-Pin': this.pin }
	}

	async #fetch(path, init) {
		try {
			return await fetch(this.base + path, { ...init, signal: AbortSignal.timeout(this.timeoutMs) })
		} catch (e) {
			throw new ApiError(
				0,
				e && e.name === 'TimeoutError'
					? `FeedView at ${this.host}:${this.port} didn't answer`
					: `Can't reach FeedView at ${this.host}:${this.port}`,
			)
		}
	}
}

module.exports = { FeedViewClient, ApiError }
