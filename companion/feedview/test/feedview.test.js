// The module against the real FeedView: every action through the real web remote API, with
// FeedView's state, variables and feedbacks checked after each one. Needs FEEDVIEW_EXE and
// FEEDVIEW_SENDER (ctest sets them); skipped otherwise. FeedView goes fullscreen briefly.
// Everything it changes is put back: settings live in a temporary file, and the system
// volume/mute/sound output are only set to their current values or moved by one step and back.
const { test, before, after } = require('node:test')
const assert = require('node:assert/strict')
const { spawn } = require('node:child_process')
const fs = require('node:fs')
const os = require('node:os')
const path = require('node:path')
const { FakeCompanion } = require('./helpers/fake-companion')
const { FeedViewClient } = require('../src/api')
const { NONE } = require('../src/choices')

const exe = process.env.FEEDVIEW_EXE
const senderExe = process.env.FEEDVIEW_SENDER
const skip = !exe || !senderExe ? 'set FEEDVIEW_EXE and FEEDVIEW_SENDER to run against FeedView' : false

const tag = `Companion Test ${process.pid}`
let app, sender, comp, api, dir, source
const state = () => api.state()

async function waitFor(cond, ms = 8000, what = 'condition') {
	const end = Date.now() + ms
	for (;;) {
		const v = await cond()
		if (v) return v
		if (Date.now() > end) throw new Error(`Timed out waiting for ${what}`)
		await new Promise((r) => setTimeout(r, 100))
	}
}

before(async () => {
	if (skip) return
	dir = fs.mkdtempSync(path.join(os.tmpdir(), 'feedview-companion-'))
	sender = spawn(senderExe, ['--name', tag, '--size', '640x360', '--fps', '30', '--seconds', '240'], {
		stdio: 'ignore',
	})
	await new Promise((r) => setTimeout(r, 1000))
	const port = 23000 + (process.pid % 500) * 2
	app = spawn(
		exe,
		['--settings', path.join(dir, 'settings.ini'), '--remote-port', String(port), '--extra-ips', '127.0.0.1'],
		{
			stdio: 'ignore',
		},
	)
	api = new FeedViewClient({ host: '127.0.0.1', port, pin: '' })
	await waitFor(
		() =>
			api.ping().then(
				() => true,
				() => false,
			),
		20000,
		'FeedView to start',
	)
	comp = new FakeCompanion()
	await comp.start({ host: '127.0.0.1', port, pin: '', poll: 200 })
	await comp.waitFor(() => comp.status && comp.status.status === 'ok', 8000, 'connection')
})

after(async () => {
	if (skip) return
	await comp?.stop()
	app?.kill()
	sender?.kill()
	if (dir) fs.rmSync(dir, { recursive: true, force: true })
})

async function act(actionId, options = {}) {
	const res = await comp.action(actionId, options)
	assert.equal(res.success, true, `${actionId}: ${res.errorMessage}`)
}

test('[REQ-15][REQ-16] a fresh FeedView has None; the module offers None first and finds the test source', { skip }, async () => {
	assert.equal(comp.variables.source_name, 'None')
	assert.equal(comp.variables.signal, 'None')
	const none = await comp.subscribe('source_on_output', { source: NONE })
	await comp.waitFor(() => comp.feedbackValues[none] === true)
	source = await waitFor(
		async () => (await state()).sources.find((s) => s.endsWith(`(${tag})`)),
		30000,
		'the test source',
	)
	await comp.waitFor(
		() => {
			const ids = comp.actions.source.options.find((o) => o.id === 'source').choices.map((c) => c.id)
			return ids[0] === NONE && ids.includes(source)
		},
		3000,
		'the source in the dropdown',
	)
})

test('[REQ-17][REQ-18][REQ-14] sources: take one (fade), select None and Take (cut), feedbacks and variables follow', { skip }, async () => {
	const onOut = await comp.subscribe('source_on_output', { source })
	await act('source', { source, transition: 'default', fade_ms: 500 })
	await waitFor(async () => (await state()).source.hasPicture, 15000, 'picture')
	await comp.waitFor(() => comp.variables.signal === 'Live' && comp.feedbackValues[onOut] === true, 3000)
	assert.equal(comp.variables.source_name, tag)
	assert.match(comp.variables.resolution, /^640x360$/)

	await act('source_select', { source: NONE })
	assert.equal((await state()).source.name, source, 'selecting changes nothing yet')
	await act('take', { transition: 'cut', fade_ms: 500 })
	await waitFor(
		async () => {
			const s = await state()
			return s.source.name === '' && !s.output.picture
		},
		5000,
		'None on the output',
	)
	await comp.waitFor(() => comp.feedbackValues[onOut] === false && comp.variables.source_name === 'None', 3000)

	await act('source', { source, transition: 'fade', fade_ms: 300 })
	await waitFor(async () => (await state()).source.hasPicture, 15000, 'picture again')
	await act('reconnect', { transition: 'default' })
	await waitFor(async () => (await state()).source.hasPicture, 15000, 'picture after reconnect')
})

test('[REQ-17][REQ-03] displays and fullscreen', { skip }, async () => {
	const full = await comp.subscribe('fullscreen', {})
	await act('fullscreen', { on: '1' })
	await waitFor(async () => (await state()).output.fullscreen, 5000, 'fullscreen')
	await comp.waitFor(() => comp.feedbackValues[full] === true, 3000)
	const onDisplay = await comp.subscribe('display_on_output', { display: (await state()).output.window.display })
	await comp.waitFor(() => comp.feedbackValues[onDisplay] === true, 3000)
	await act('fullscreen', { on: '0' })
	await waitFor(async () => !(await state()).output.fullscreen, 5000, 'windowed')
	await act('display', { display: 1, fullscreen: '0' })
	await act('display_select', { display: 1 })
	await act('take', {})
	await waitFor(async () => (await state()).output.fullscreen, 5000, 'fullscreen on display 1')
	await act('fullscreen', { on: '0' })
	// Display numbers need two screens; with one FeedView says so and the action fails.
	const displays = (await state()).displays.length
	const res = await comp.action('identify', { on: '1' })
	if (displays > 1) {
		assert.equal(res.success, true)
		await act('identify', { on: '0' })
	} else {
		assert.equal(res.success, false)
		assert.match(res.errorMessage, /one display/)
	}
})

test("[REQ-17][REQ-04] FeedView's controls: hide", { skip }, async () => {
	await act('controls', { on: '0' })
	const s = await state()
	assert.equal(s.output.controls.visible, false)
	assert.equal(s.output.controls.panel, '')
})

test('[REQ-17][REQ-07][REQ-08] audio: system volume, mute, sound output, channels (all put back)', { skip }, async () => {
	const s0 = await state()
	const v0 = s0.audio.volume
	const v1 = v0 < 100 ? v0 + 1 : v0 - 1
	await act('volume', { value: v1 })
	assert.equal((await state()).audio.volume, v1)
	await comp.waitFor(() => comp.variables.volume === v1, 3000)
	await act('volume_step', { step: v0 - v1 })
	assert.equal((await state()).audio.volume, v0)
	await act('mute', { on: s0.audio.muted ? '1' : '0' })
	assert.equal((await state()).audio.muted, s0.audio.muted)
	if (s0.audio.system && s0.audio.output) {
		await act('audio_output', { output: s0.audio.output })
		assert.equal((await state()).audio.output, s0.audio.output)
	}
	await act('audio_pair', { first: 3 })
	assert.equal((await state()).audio.firstChannel, 3)
	await act('audio_pair', { first: 1 })
	assert.equal((await state()).audio.firstChannel, 1)
})

test('[REQ-17][REQ-02][REQ-05][REQ-18] settings: each on/off setting toggles and back; fade time; extra IPs', { skip }, async () => {
	const keys = {
		clean_output: 'cleanOutput',
		start_fullscreen: 'startFullscreen',
		show_info: 'showInfo',
		always_on_top: 'alwaysOnTop',
		silence_notifications: 'silenceNotifications',
	}
	for (const [setting, key] of Object.entries(keys)) {
		const before = (await state()).settings[key]
		const fb = await comp.subscribe('setting', { setting })
		await act('setting', { setting, on: 'toggle' })
		assert.equal((await state()).settings[key], !before, setting)
		await comp.waitFor(() => comp.feedbackValues[fb] === !before, 3000, `${setting} feedback`)
		await act('setting', { setting, on: before ? '1' : '0' })
		assert.equal((await state()).settings[key], before, setting)
	}
	await act('fade_time', { ms: 750 })
	assert.equal((await state()).settings.fadeMs, 750)
	await comp.waitFor(() => comp.variables.fade_ms === 750, 3000)
	await act('fade_time', { ms: 500 })
	await act('extra_ips', { ips: '127.0.0.1' })
	assert.equal((await state()).settings.extraIps, '127.0.0.1')
	await act('allow_notification_control', {})
})

test('[REQ-16] FeedView refuses a bad value: the action fails with its message', { skip }, async () => {
	const res = await comp.action('display', { display: 99, fullscreen: '1' })
	assert.equal(res.success, false)
	assert.match(res.errorMessage, /no display 99/i)
	assert.equal(comp.status.status, 'ok')
})
