// Plays Companion's part: starts the module as Companion does (a child process speaking
// @companion-module/base's IPC protocol), and records what the module sends back.
const { fork } = require('node:child_process')
const path = require('node:path')
const EJSON = require('ejson')

const ROOT = path.join(__dirname, '..', '..')

class FakeCompanion {
	constructor() {
		this.status = null // {status, message}
		this.actions = {} // id -> definition
		this.feedbacks = {}
		this.variableDefinitions = []
		this.variables = {}
		this.presets = []
		this.feedbackValues = {}
		this.logs = []
		this.definitionUpdates = 0
		this.pending = new Map()
		this.nextId = 1
		this.nextInstance = 1
	}

	/**
	 * @param {object} config the connection's saved settings
	 * @param {{dir?: string}} [where] dir = a packaged module (default: the sources)
	 */
	async start(config, where = {}) {
		this.label = 'feedview'
		const entry = where.dir ? path.join(where.dir, 'main.js') : path.join(ROOT, 'src', 'main.js')
		const manifest = path.join(where.dir || ROOT, 'companion', 'manifest.json')
		this.child = fork(entry, [], {
			env: {
				...process.env,
				MODULE_MANIFEST: manifest,
				CONNECTION_ID: 'test-connection',
				VERIFICATION_TOKEN: 'test-token',
			},
			stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
		})
		this.child.stdout.on('data', () => {})
		this.child.stderr.on('data', (d) => this.logs.push({ level: 'stderr', message: String(d) }))
		this.child.on('message', (msg) => this.#onMessage(msg))
		this.exited = new Promise((resolve) => this.child.once('exit', resolve))
		await this.waitFor(() => this.registered, 5000, 'module registration')
		return this.call('init', {
			label: this.label,
			isFirstInit: false, // an existing connection with saved settings (a first init starts from the defaults)
			config,
			secrets: {},
			lastUpgradeIndex: -1,
			feedbacks: {},
			actions: {},
		})
	}

	async stop() {
		if (!this.child || this.child.exitCode !== null) return
		await this.call('destroy', {}).catch(() => {})
		this.child.kill()
		await this.exited
	}

	updateConfig(config) {
		return this.call('updateConfigAndLabel', { label: this.label, config, secrets: {} })
	}

	/** Runs an action like a button press; resolves with {success, errorMessage}. */
	action(actionId, options = {}) {
		const id = `action-${this.nextInstance++}`
		return this.call('executeAction', {
			action: { id, controlId: `control-${id}`, actionId, options, upgradeIndex: null, disabled: false },
			surfaceId: undefined,
		})
	}

	/** Puts a feedback on a button; its value then follows in feedbackValues[id]. */
	async subscribe(feedbackId, options = {}) {
		const id = `feedback-${this.nextInstance++}`
		await this.call('updateFeedbacks', {
			feedbacks: {
				[id]: {
					id,
					controlId: `control-${id}`,
					feedbackId,
					options,
					isInverted: false,
					upgradeIndex: null,
					disabled: false,
				},
			},
		})
		return id
	}

	call(name, payload) {
		const callbackId = this.nextId++
		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this.pending.delete(callbackId)
				reject(new Error(`${name} timed out`))
			}, 10000)
			this.pending.set(callbackId, { resolve, reject, timer })
			this.child.send({ direction: 'call', name, payload: EJSON.stringify(payload), callbackId })
		})
	}

	async waitFor(cond, timeoutMs = 5000, what = 'condition') {
		const end = Date.now() + timeoutMs
		while (Date.now() < end) {
			if (cond()) return
			await new Promise((r) => setTimeout(r, 20))
		}
		if (!cond()) throw new Error(`Timed out waiting for ${what}`)
	}

	#reply(msg, success, payload) {
		if (msg.callbackId)
			this.child.send({ direction: 'response', callbackId: msg.callbackId, success, payload: EJSON.stringify(payload) })
	}

	#onMessage(msg) {
		if (msg.direction === 'response') {
			const p = this.pending.get(msg.callbackId)
			if (!p) return
			this.pending.delete(msg.callbackId)
			clearTimeout(p.timer)
			const data = msg.payload ? EJSON.parse(msg.payload) : undefined
			if (msg.success) p.resolve(data)
			else p.reject(new Error((data && data.message) || 'call failed'))
			return
		}
		const data = msg.payload ? EJSON.parse(msg.payload) : undefined
		switch (msg.name) {
			case 'register':
				this.registered = true
				this.#reply(msg, true, {})
				break
			case 'set-status':
				this.status = data
				break
			case 'setActionDefinitions':
				this.actions = Object.fromEntries(data.actions.map((a) => [a.id, a]))
				this.definitionUpdates++
				break
			case 'setFeedbackDefinitions':
				this.feedbacks = Object.fromEntries(data.feedbacks.map((f) => [f.id, f]))
				break
			case 'setVariableDefinitions':
				this.variableDefinitions = data.variables
				break
			case 'setVariableValues':
				for (const v of data.newValues) this.variables[v.id] = v.value
				break
			case 'setPresetDefinitions':
				this.presets = data.presets
				break
			case 'updateFeedbackValues':
				for (const v of data.values) this.feedbackValues[v.id] = v.value
				break
			case 'log-message':
				this.logs.push(data)
				break
			case 'parseVariablesInString':
				this.#reply(msg, true, { text: data.text, variableIds: [] })
				break
			default:
				this.#reply(msg, true, undefined)
		}
	}
}

module.exports = { FakeCompanion }
