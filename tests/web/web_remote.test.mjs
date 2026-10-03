// FeedView's web page in a real browser (headless Edge/Chrome), against the real FeedView:
// every control on the page is used the way you would use it - tap, Take, Cut, sliders,
// lists, checkboxes - and the test checks what the page sent and that FeedView followed.
// Test names carry the IDs of your requests in tests/REQUIREMENTS.md.
//
// Needs FEEDVIEW_EXE and FEEDVIEW_SENDER (ctest sets them) and Edge or Chrome
// (FEEDVIEW_BROWSER overrides); skipped otherwise. FeedView goes fullscreen briefly; the
// system volume is moved by one step and back, every setting is put back.
import { test, before, after } from 'node:test'
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findBrowser, Page } from './browser.mjs'

const exe = process.env.FEEDVIEW_EXE
const senderExe = process.env.FEEDVIEW_SENDER
const browser = findBrowser()
const skip = !exe || !senderExe ? 'set FEEDVIEW_EXE and FEEDVIEW_SENDER' : !browser ? 'no Edge or Chrome found (FEEDVIEW_BROWSER)' : false

const tag = `Web Test ${process.pid}`
let app, sender, page, dir, base, source
const procs = []

async function state() {
	const r = await fetch(`${base}/api/state`)
	return r.json()
}
async function waitState(pred, ms = 8000, what = 'FeedView') {
	const end = Date.now() + ms
	for (;;) {
		const s = await state()
		if (s.app && pred(s)) return s // right after start FeedView's state is {} until its first frame
		if (Date.now() > end) throw new Error(`Timed out waiting for ${what}`)
		await new Promise((r) => setTimeout(r, 100))
	}
}
const params = (req) => Object.fromEntries(new URLSearchParams(req.body))
const mark = () => page.requests.length

before(async () => {
	if (skip) return
	dir = fs.mkdtempSync(path.join(os.tmpdir(), 'feedview-web-'))
	sender = spawn(senderExe, ['--name', tag, '--size', '640x360', '--fps', '30', '--seconds', '300'], { stdio: 'ignore' })
	procs.push(sender)
	const port = 24000 + (process.pid % 500) * 2
	app = spawn(exe, ['--settings', path.join(dir, 'settings.ini'), '--remote-port', String(port), '--extra-ips', '127.0.0.1'], {
		stdio: 'ignore',
	})
	procs.push(app)
	base = `http://127.0.0.1:${port}`
	const end = Date.now() + 20000
	while (!(await fetch(`${base}/api/ping`).then((r) => r.ok, () => false))) {
		if (Date.now() > end) throw new Error('FeedView did not start')
		await new Promise((r) => setTimeout(r, 200))
	}
	source = (await waitState((s) => s.sources.some((n) => n.endsWith(`(${tag})`)), 30000, 'the test source')).sources.find(
		(n) => n.endsWith(`(${tag})`),
	)
	page = await Page.open(browser)
	await page.goto(`${base}/`)
	await page.waitFor(`!document.getElementById('app').hidden && document.getElementById('linkText').textContent === 'Connected'`, 10000, 'the page to connect')
})

after(async () => {
	if (skip) return
	await page?.close()
	for (const p of procs) p.kill()
	if (dir) fs.rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 })
})

test('[REQ-09] the page opens without asking for a PIN', { skip }, async () => {
	assert.equal(await page.eval(`document.getElementById('gate').hidden`), true)
	assert.equal((await state()).remote.pinRequired, false)
})

test('[REQ-15][REQ-14] None is first in the source list while NDI sources are listed, and on the output on a fresh start', { skip }, async () => {
	await page.waitFor(`document.querySelectorAll('#sources .src').length >= 2`, 8000, 'sources in the list')
	const first = await page.eval(`(() => { const b = document.querySelector('#sources .src'); return { id: b.id, text: b.textContent, on: b.classList.contains('on') } })()`)
	assert.equal(first.id, 'srcNone')
	assert.match(first.text, /None/)
	assert.match(first.text, /On output/)
	assert.ok((await state()).sources.length >= 1, 'NDI sources are listed too')
})

const sourceButton = (name) =>
	`[...document.querySelectorAll('#sources .src')].find((b) => b.textContent.includes(${JSON.stringify(name)}))`

test('[REQ-06][REQ-18] a source: tap it (green), then Take - with the fade from Settings', { skip }, async () => {
	const short = source.slice(source.indexOf(' (') + 2, -1)
	await page.waitFor(`!!${sourceButton(short)}`, 5000, 'the test source in the list')
	await page.eval(`${sourceButton(short)}.click()`)
	assert.equal(await page.eval(`document.getElementById('takebar').hidden`), false)
	assert.equal(await page.eval(`${sourceButton(short)}.classList.contains('sel')`), true, 'green while selected')
	assert.equal((await state()).source.name, '', 'nothing changes before Take')
	const m = mark()
	await page.click('#takeBtn')
	const req = await page.request('/api/source', m)
	assert.deepEqual(params(req), { name: source })
	await waitState((s) => s.source.name === source && s.source.hasPicture, 15000, 'the picture')
	await page.waitFor(`${sourceButton(short)}.classList.contains('on')`, 5000, 'red: on output')
})

test('[REQ-15][REQ-14] None: tap it, then Take - the output goes black', { skip }, async () => {
	await page.click('#srcNone')
	const m = mark()
	await page.click('#takeBtn')
	assert.deepEqual(params(await page.request('/api/source', m)), { name: '' })
	await waitState((s) => s.source.name === '' && !s.output.picture, 6000, 'None on the output')
	await page.waitFor(`document.getElementById('srcNone').classList.contains('on')`, 5000, 'None red')
})

test('[REQ-18] Cut takes a source without a fade; Take uses the fade setting', { skip }, async () => {
	const short = source.slice(source.indexOf(' (') + 2, -1)
	await page.eval(`${sourceButton(short)}.click()`)
	assert.equal(await page.eval(`document.getElementById('cutBtn').hidden`), false, 'Cut is offered for sources')
	const m = mark()
	await page.click('#cutBtn')
	assert.deepEqual(params(await page.request('/api/source', m)), { name: source, fade_ms: '0' })
	await waitState((s) => s.source.name === source && s.source.hasPicture, 15000, 'the picture')
})

test('[REQ-18][REQ-06] the fade time is set in the page\'s Settings', { skip }, async () => {
	await page.eval(`document.getElementById('settings').open = true`)
	const options = await page.eval(`[...document.querySelectorAll('#setFade option')].map((o) => o.value)`)
	assert.ok(options.includes('0') && options.includes('500') && options.includes('1000'))
	const m = mark()
	await page.eval(`(() => { const s = document.getElementById('setFade'); s.value = '1000'; s.dispatchEvent(new Event('change')) })()`)
	assert.deepEqual(params(await page.request('/api/settings', m)), { fade_ms: '1000' })
	await waitState((s) => s.settings.fadeMs === 1000)
	await page.eval(`(() => { const s = document.getElementById('setFade'); s.value = '500'; s.dispatchEvent(new Event('change')) })()`)
	await waitState((s) => s.settings.fadeMs === 500)
})

test('[REQ-03][REQ-06] the Fullscreen button puts FeedView fullscreen, and back', { skip }, async () => {
	const m = mark()
	await page.click('#fsBtn')
	assert.deepEqual(params(await page.request('/api/fullscreen', m)), { on: '1' })
	await waitState((s) => s.output.fullscreen && s.output.onTop, 5000, 'fullscreen, on top')
	await page.waitFor(`document.getElementById('fsBtn').textContent === 'Leave fullscreen'`)
	await page.click('#fsBtn')
	await waitState((s) => !s.output.fullscreen, 5000, 'windowed')
})

test('[REQ-06] a screen: tap it (green), then Take - FeedView goes fullscreen there', { skip }, async () => {
	await page.waitFor(`document.querySelectorAll('#screens button').length >= 1`)
	await page.click('#screens button')
	assert.equal(await page.eval(`document.getElementById('takebar').hidden`), false)
	assert.equal(await page.eval(`document.getElementById('cutBtn').hidden`), true, 'no Cut for screens')
	const m = mark()
	await page.click('#takeBtn')
	assert.deepEqual(params(await page.request('/api/display', m)), { number: '1', fullscreen: '1' })
	await waitState((s) => s.output.fullscreen && s.output.window.display === 1, 5000, 'fullscreen on display 1')
	await page.click('#fsBtn')
	await waitState((s) => !s.output.fullscreen, 5000, 'windowed')
})

test("[REQ-04][REQ-06] the page shows when FeedView's controls/panels are on its screen, and Hide removes them", { skip }, async () => {
	const r = await fetch(`${base}/api/controls`, { method: 'POST', headers: { 'X-FeedView-Pin': '' }, body: new URLSearchParams({ on: '1' }) })
	assert.equal(r.status, 200)
	await page.waitFor(`!document.getElementById('ctlNote').hidden`, 5000, 'the note on the page')
	const m = mark()
	await page.click('#ctlHide')
	assert.deepEqual(params(await page.request('/api/controls', m)), { on: '0' })
	await waitState((s) => !s.output.controls.visible && s.output.controls.panel === '', 3000, 'controls hidden')
	await page.waitFor(`document.getElementById('ctlNote').hidden`, 5000, 'the note gone')
})

test('[REQ-07][REQ-06] the volume slider sets the system volume (and back)', { skip }, async () => {
	const s0 = await state()
	assert.equal(s0.audio.system, true, 'the volume is the system volume on this computer')
	const v0 = s0.audio.volume
	const v1 = v0 < 100 ? v0 + 1 : v0 - 1
	const slide = (v) =>
		page.eval(`(() => { const s = document.getElementById('vol'); s.value = ${v}; s.dispatchEvent(new Event('input')); s.dispatchEvent(new Event('change')) })()`)
	const m = mark()
	await slide(v1)
	assert.deepEqual(params(await page.request('/api/volume', m)), { value: String(v1) })
	await waitState((s) => s.audio.volume === v1, 3000, 'the system volume')
	await slide(v0)
	await waitState((s) => s.audio.volume === v0, 3000, 'the volume back')
})

test('[REQ-07][REQ-06] the Mute button mutes and unmutes', { skip }, async () => {
	const muted0 = (await state()).audio.muted
	await page.click('#muteBtn')
	await waitState((s) => s.audio.muted === !muted0, 3000, 'mute toggled')
	await page.waitFor(`document.getElementById('muteBtn').textContent === ${JSON.stringify(muted0 ? 'Mute' : 'Muted')}`)
	await page.click('#muteBtn')
	await waitState((s) => s.audio.muted === muted0, 3000, 'mute back')
})

test("[REQ-08][REQ-06] the sound output list is the computer's outputs, and choosing one sets it", { skip }, async () => {
	const s = await state()
	const ids = await page.eval(`[...document.querySelectorAll('#outSel option')].map((o) => o.value)`)
	assert.deepEqual(ids, s.audio.outputs.map((o) => o.id))
	assert.equal(await page.eval(`document.getElementById('outSel').value`), s.audio.output)
	const m = mark()
	await page.eval(`document.getElementById('outSel').dispatchEvent(new Event('change'))`)
	assert.deepEqual(params(await page.request('/api/audio-output', m)), { id: s.audio.output })
	await waitState((x) => x.audio.output === s.audio.output)
})

test('[REQ-06] the audio channel buttons, Reconnect and Show display numbers', { skip }, async () => {
	let m = mark()
	await page.click('#pairs button')
	assert.deepEqual(params(await page.request('/api/audio-pair', m)), { first: '1' })
	m = mark()
	await page.click('#reconnectBtn')
	await page.request('/api/reconnect', m)
	await waitState((s) => s.source.hasPicture, 15000, 'the picture after reconnecting')
	const displays = (await state()).displays.length
	const disabled = await page.eval(`document.getElementById('idBtn').disabled`)
	if (displays < 2) {
		assert.equal(disabled, true, 'one screen: nothing to tell apart')
	} else {
		m = mark()
		await page.click('#idBtn')
		await waitState((s) => s.output.identify, 3000, 'display numbers')
		await page.click('#idBtn')
		await waitState((s) => !s.output.identify, 3000, 'display numbers off')
	}
})

test('[REQ-06][REQ-02][REQ-05] every setting checkbox switches its setting (and back)', { skip }, async () => {
	await page.eval(`document.getElementById('settings').open = true`)
	const boxes = {
		setClean: ['clean_output', 'cleanOutput'],
		setStartFs: ['start_fullscreen', 'startFullscreen'],
		setInfo: ['show_info', 'showInfo'],
		setTop: ['always_on_top', 'alwaysOnTop'],
		setQuiet: ['silence_notifications', 'silenceNotifications'],
	}
	for (const [id, [param, key]] of Object.entries(boxes)) {
		const before = (await state()).settings[key]
		assert.equal(await page.eval(`document.getElementById('${id}').checked`), before, `${id} shows the setting`)
		const m = mark()
		await page.click(`#${id}`)
		assert.deepEqual(params(await page.request('/api/settings', m)), { [param]: before ? '0' : '1' }, id)
		await waitState((s) => s.settings[key] === !before, 3000, id)
		await page.click(`#${id}`)
		await waitState((s) => s.settings[key] === before, 3000, `${id} back`)
	}
})

test('[REQ-06] extra discovery IPs are saved from the page', { skip }, async () => {
	await page.eval(`document.getElementById('setIps').value = '127.0.0.1'`)
	const m = mark()
	await page.click('#saveIps')
	assert.deepEqual(params(await page.request('/api/settings', m)), { extra_ips: '127.0.0.1' })
	await waitState((s) => s.settings.extraIps === '127.0.0.1')
})

test('[REQ-05] notifications: the page says they are off, or offers the permission FeedView needs', { skip }, async () => {
	const n = (await state()).notifications
	const noteHidden = await page.eval(`document.getElementById('quietNote').hidden`)
	if (n.silenced) assert.equal(noteHidden, true)
	else {
		assert.equal(noteHidden, false)
		assert.equal(await page.eval(`document.getElementById('quietAllow').hidden`), n.permitted)
	}
	assert.match(await page.eval(`document.getElementById('quietText').textContent`), /Notifications/)
})

test('[REQ-06] every command the page can send was used above', { skip }, async () => {
	const html = fs.readFileSync(path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..', 'web', 'index.html'), 'utf8')
	const commands = new Set([...html.matchAll(/cmd\("([a-z-]+)"/g), ...html.matchAll(/api\("\/api\/([a-z-]+)"/g)].map((x) => x[1]))
	commands.delete('state')
	// Shown only when FeedView lacks the permission; it can't be clicked on a computer that has it.
	if ((await state()).notifications.permitted) commands.delete('allow-notification-control')
	if ((await state()).displays.length < 2) commands.delete('identify')
	const used = new Set(page.requests.filter((r) => r.method === 'POST').map((r) => r.path.replace('/api/', '')))
	for (const c of commands) assert.ok(used.has(c), `the page's "${c}" was not tested`)
})
