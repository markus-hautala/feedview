// The module's logic without Companion or FeedView: config, choices, variables, feedbacks.
const { test } = require('node:test')
const assert = require('node:assert/strict')
const { normalizeConfig } = require('../src/config')
const {
	NONE,
	sourceChoices,
	toSourceName,
	toSourceId,
	splitSourceName,
	displayChoices,
	choicesSignature,
} = require('../src/choices')
const { variableValues, VARIABLES } = require('../src/variables')
const { is } = require('../src/feedbacks')
const { fadeParam } = require('../src/actions')
const { baseState } = require('./helpers/fake-feedview')

test('[REQ-16] config: the address copied from FeedView works as the host; IPv6 and defaults', () => {
	assert.deepEqual(normalizeConfig({ host: 'http://192.168.1.20:8081/#pin=1234', port: 8080 }), {
		host: '192.168.1.20',
		port: 8081,
		pin: '',
		poll: 500,
	})
	assert.equal(normalizeConfig({ host: 'studio-pc.local' }).host, 'studio-pc.local')
	assert.equal(normalizeConfig({ host: 'fe80::1', port: 9000 }).host, 'fe80::1')
	assert.deepEqual(normalizeConfig({ host: '[fe80::1]:9001' }), { host: 'fe80::1', port: 9001, pin: '', poll: 500 })
	assert.deepEqual(normalizeConfig(undefined), { host: '127.0.0.1', port: 8080, pin: '', poll: 500 })
	assert.equal(normalizeConfig({ poll: 5 }).poll, 100)
	assert.equal(normalizeConfig({ pin: ' 0666 ' }).pin, '0666')
})

test('[REQ-15][REQ-16] sources: None (black) is always the first choice, even with NDI sources; the offline current one stays', () => {
	const s = baseState()
	const c = sourceChoices(s)
	assert.deepEqual(c[0], { id: NONE, label: 'None (black output)' })
	assert.deepEqual(
		c.map((x) => x.id),
		[NONE, 'CAM-PC (Camera 1)', 'GFX-PC (Program)'],
	)
	s.source.name = 'OLD-PC (Gone)'
	assert.equal(sourceChoices(s)[1].id, 'OLD-PC (Gone)')
	assert.deepEqual(sourceChoices(null), [{ id: NONE, label: 'None (black output)' }])
	assert.equal(toSourceName(NONE), '')
	assert.equal(toSourceName('A (B)'), 'A (B)')
	assert.equal(toSourceId(''), NONE)
	assert.deepEqual(splitSourceName('STUDIO-PC (Program (2))'), { name: 'Program (2)', machine: 'STUDIO-PC' })
})

test('[REQ-16] displays: numbered like FeedView; placeholders while not connected', () => {
	assert.deepEqual(
		displayChoices(baseState()).map((d) => d.label),
		['1: Laptop (1920x1080)', '2: Projector (1920x1080)'],
	)
	assert.equal(displayChoices(null).length, 4)
	const a = baseState()
	const b = baseState()
	assert.equal(choicesSignature(a), choicesSignature(b))
	b.sources.push('NEW (Source)')
	assert.notEqual(choicesSignature(a), choicesSignature(b))
	b.source.hasPicture = false // state changes that don't touch the choices don't rebuild them
	assert.equal(choicesSignature(b), choicesSignature({ ...b, audio: { ...b.audio, volume: 3 } }))
})

test('[REQ-16] variables: from the state; every defined variable gets a value; "Not connected" while offline', () => {
	const v = variableValues(baseState(), { label: 'Program' })
	assert.equal(v.connected, true)
	assert.equal(v.source, 'CAM-PC (Camera 1)')
	assert.equal(v.source_name, 'Camera 1')
	assert.equal(v.source_machine, 'CAM-PC')
	assert.equal(v.signal, 'Live')
	assert.equal(v.resolution, '1920x1080')
	assert.equal(v.frame_rate, '50p')
	assert.equal(v.fps, 50)
	assert.equal(v.audio_format, '48 kHz, 4 ch')
	assert.equal(v.audio_channels, 'Ch 1-2')
	assert.equal(v.volume, 40)
	assert.equal(v.sound_output, 'Speakers (Realtek(R) Audio)')
	assert.equal(v.display, 2)
	assert.equal(v.display_name, 'Projector')
	assert.equal(v.fade_ms, 500)
	assert.equal(v.selected, 'Program')
	assert.equal(v.last_event, 'Fullscreen on 2: Projector (1920x1080)')
	assert.match(v.last_event_time, /^\d\d:\d\d:\d\d$/)
	for (const d of VARIABLES) assert.ok(d.variableId in v, d.variableId)

	const none = baseState()
	none.source.name = ''
	assert.equal(variableValues(none, null).source_name, 'None')
	assert.equal(variableValues(none, null).signal, 'None')
	const lost = baseState()
	lost.source.signalLost = true
	lost.source.connected = false
	assert.equal(variableValues(lost, null).signal, 'Source offline')

	const off = variableValues(null, null)
	assert.equal(off.connected, false)
	assert.equal(off.signal, 'Not connected')
	for (const d of VARIABLES) assert.ok(d.variableId in off, d.variableId)
})

test('[REQ-16] feedbacks: red on the output, green when selected, warnings; all false while offline', () => {
	const s = baseState()
	assert.equal(is.sourceOnOutput(s, 'CAM-PC (Camera 1)'), true)
	assert.equal(is.sourceOnOutput(s, NONE), false)
	s.source.name = ''
	assert.equal(is.sourceOnOutput(s, NONE), true)
	assert.equal(is.sourceSelected(s, { kind: 'source', value: '' }, NONE), true)
	assert.equal(is.sourceSelected(s, { kind: 'display', value: 1 }, NONE), false)
	assert.equal(is.displayOnOutput(s, 2), true)
	assert.equal(is.displayOnOutput(s, '1'), false)
	s.output.fullscreen = false
	assert.equal(is.displayOnOutput(s, 2), false) // a window on it isn't "on output"
	assert.equal(is.displaySelected(s, { kind: 'display', value: 2 }, '2'), true)
	assert.equal(is.audioOutput(s, '{out-a}'), true)
	assert.equal(is.audioPair(s, '1'), true)
	assert.equal(is.setting(s, 'clean_output'), true)
	assert.equal(is.setting(s, 'show_info'), false)
	assert.equal(is.fadeTime(s, 500), true)
	assert.equal(is.noPicture(s), false) // None has no picture by design: not a warning
	s.source.name = 'X (Y)'
	s.source.hasPicture = false
	assert.equal(is.noPicture(s), true)
	// Offline: only "not reachable" is on. (A selection is the module's own, so it stays.)
	const own = ['sourceSelected', 'displaySelected', 'selectionPending']
	for (const [name, fn] of Object.entries(is)) {
		if (name === 'disconnected') assert.equal(fn(null), true)
		else if (!own.includes(name)) assert.equal(fn(null, 1), false, name)
	}
})

test('[REQ-18][REQ-17] transitions: the fade_ms parameter of a take', () => {
	assert.deepEqual(fadeParam({ transition: 'default' }), {})
	assert.deepEqual(fadeParam({ transition: 'cut' }), { fade_ms: 0 })
	assert.deepEqual(fadeParam({ transition: 'fade', fade_ms: 1200 }), { fade_ms: 1200 })
	assert.deepEqual(fadeParam({ transition: 'fade', fade_ms: 99999 }), { fade_ms: 10000 })
	assert.deepEqual(fadeParam({ transition: 'fade', fade_ms: 'x' }), { fade_ms: 500 })
})
