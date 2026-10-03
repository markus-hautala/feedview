// Companion module for FeedView: the same controls as FeedView's web page, through its HTTP API.
const { InstanceBase, InstanceStatus, runEntrypoint } = require('@companion-module/base')
const { FeedViewClient } = require('./api')
const { getConfigFields, normalizeConfig } = require('./config')
const { buildActions } = require('./actions')
const { buildFeedbacks } = require('./feedbacks')
const { buildPresets } = require('./presets')
const { VARIABLES, variableValues } = require('./variables')
const { choicesSignature, splitSourceName } = require('./choices')
const UpgradeScripts = require('./upgrades')

// After a 401 the module only pings (which needs no PIN), so a wrong PIN is never sent again:
// FeedView pauses an address for a minute after five wrong PINs.
const AUTH_RETRY_MS = 5000
const OFFLINE_RETRY_MS = 2000
const LOCKED_RETRY_MS = 15000

class FeedViewInstance extends InstanceBase {
	constructor(internal) {
		super(internal)
		this.state = null // FeedView's last /api/state, null while not connected
		this.selection = null // {kind: 'source'|'display', value, label}, waiting for Take
		this.timer = null
		this.running = false
		this.authBlocked = false
		this.signature = null
		this.generation = 0 // bumped on restart, so an old poll can't touch the new connection
	}

	async init(config) {
		this.config = normalizeConfig(config)
		this.setVariableDefinitions(VARIABLES)
		this.updateDefinitions()
		this.setVariableValues(variableValues(null, null))
		this.start()
	}

	async destroy() {
		this.stop()
	}

	async configUpdated(config) {
		this.stop()
		this.config = normalizeConfig(config)
		this.setOffline()
		this.start()
	}

	getConfigFields() {
		return getConfigFields()
	}

	// ---- Connection

	start() {
		this.client = new FeedViewClient(this.config)
		this.running = true
		this.authBlocked = false
		this.generation++
		this.updateStatus(InstanceStatus.Connecting, `${this.config.host}:${this.config.port}`)
		this.schedule(0)
	}

	stop() {
		this.running = false
		this.generation++
		clearTimeout(this.timer)
		this.timer = null
	}

	schedule(ms) {
		clearTimeout(this.timer)
		if (!this.running) return
		const gen = this.generation
		this.timer = setTimeout(() => this.poll(gen), ms)
	}

	async poll(gen) {
		try {
			if (this.authBlocked) {
				const ping = await this.client.ping()
				if (gen !== this.generation) return
				if (ping.pinRequired) {
					this.schedule(AUTH_RETRY_MS)
					return
				}
				this.authBlocked = false // FeedView doesn't want a PIN any more
			}
			const state = await this.client.state()
			if (gen !== this.generation) return
			const wasOffline = !this.state
			this.applyState(state)
			this.updateStatus(InstanceStatus.Ok)
			if (wasOffline) this.log('info', `Connected to FeedView ${state.app.version} on ${state.app.host}`)
			this.schedule(this.config.poll)
		} catch (e) {
			if (gen !== this.generation) return
			this.handleError(e)
		}
	}

	handleError(e) {
		const status = e && e.status
		if (status === 401) {
			this.authBlocked = true
			this.updateStatus(
				InstanceStatus.AuthenticationFailure,
				this.config.pin
					? "Wrong PIN: check it in FeedView's Remote panel"
					: 'FeedView requires a PIN: enter it in the connection settings',
			)
			this.setOffline()
			this.schedule(AUTH_RETRY_MS)
		} else if (status === 403) {
			// Without a PIN FeedView only accepts IP addresses and local names (DNS-rebinding protection).
			this.updateStatus(InstanceStatus.BadConfig, e.message)
			this.setOffline()
			this.schedule(AUTH_RETRY_MS)
		} else if (status === 429) {
			this.updateStatus(
				InstanceStatus.ConnectionFailure,
				'Too many wrong PINs: FeedView pauses this address for a minute',
			)
			this.setOffline()
			this.schedule(LOCKED_RETRY_MS)
		} else {
			if (this.state) this.log('warn', e.message)
			this.updateStatus(InstanceStatus.ConnectionFailure, e.message)
			this.setOffline()
			this.schedule(OFFLINE_RETRY_MS)
		}
	}

	setOffline() {
		if (this.state === null && this.signature !== null) return
		this.state = null
		this.publish()
	}

	// ---- State -> variables, feedbacks, choices

	applyState(state) {
		this.state = state
		// Drop a selection that no longer makes sense (as the web page does).
		const sel = this.selection
		if (sel && sel.kind === 'display' && !(state.displays || []).some((d) => d.number === sel.value))
			this.selection = null
		this.publish()
	}

	publish() {
		const sig = choicesSignature(this.state)
		if (sig !== this.signature) {
			this.signature = sig
			this.updateDefinitions()
		}
		this.setVariableValues(variableValues(this.state, this.selection))
		this.checkFeedbacks()
	}

	updateDefinitions() {
		this.setActionDefinitions(buildActions(this))
		this.setFeedbackDefinitions(buildFeedbacks(this))
		this.setPresetDefinitions(buildPresets(this))
	}

	// ---- Used by the actions

	/** Runs a FeedView command; throws (so Companion logs it) when FeedView refuses it. */
	async run(action, params) {
		let res
		try {
			res = await this.client.command(action, params)
		} catch (e) {
			this.handleError(e)
			throw e
		}
		if (res.state) this.applyState(res.state)
		if (!res.ok) throw new Error(res.message || `FeedView refused ${action}`)
		return res
	}

	/** Selects a source ('' = None) or display for Take; null clears the selection. */
	select(kind, value) {
		if (!kind) {
			this.selection = null
		} else if (kind === 'source') {
			this.selection = { kind, value, label: value ? splitSourceName(value).name : 'None (black)' }
		} else {
			this.selection = { kind, value, label: `Display ${value}` }
		}
		this.setVariableValues({ selected: this.selection ? this.selection.label : '' })
		this.checkFeedbacks('source_selected', 'display_selected', 'selection_pending')
	}

	/** Puts the selection on the output. `fade`: {fade_ms} for a source, or {} for FeedView's setting. */
	async take(fade) {
		const sel = this.selection
		if (!sel) throw new Error('Nothing is selected: select a source or a display first')
		if (sel.kind === 'source') await this.run('source', { name: sel.value, ...fade })
		else await this.run('display', { number: sel.value, fullscreen: 1 })
		if (this.selection === sel) this.select(null)
	}
}

runEntrypoint(FeedViewInstance, UpgradeScripts)
