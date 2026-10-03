// Dropdown choices, built from what FeedView reports (its sources, screens and sound outputs).

// Companion dropdowns need a non-empty id; FeedView's None (black output) is the empty name.
const NONE = '__none__'
const NONE_LABEL = 'None (black output)'

const SWITCH = [
	{ id: 'toggle', label: 'Toggle' },
	{ id: '1', label: 'On' },
	{ id: '0', label: 'Off' },
]

// FeedView's on/off settings: API parameter, state key, label.
const SETTINGS = [
	{ id: 'clean_output', key: 'cleanOutput', label: 'Clean output (nothing over the fullscreen picture)' },
	{ id: 'start_fullscreen', key: 'startFullscreen', label: 'Start in fullscreen' },
	{ id: 'show_info', key: 'showInfo', label: 'Always show the info line' },
	{ id: 'always_on_top', key: 'alwaysOnTop', label: 'Always on top' },
	{ id: 'silence_notifications', key: 'silenceNotifications', label: 'Silence notifications' },
]

const TRANSITIONS = [
	{ id: 'default', label: 'Fade as set in FeedView' },
	{ id: 'cut', label: 'Cut' },
	{ id: 'fade', label: 'Fade, with this time' },
]

const AUDIO_PAIRS = Array.from({ length: 8 }, (_, i) => ({ id: 2 * i + 1, label: `Ch ${2 * i + 1}-${2 * i + 2}` }))

/** "MACHINE (Source)" -> {name: "Source", machine: "MACHINE"} */
function splitSourceName(full) {
	const n = String(full || '')
	// Computer names can't contain " (", source names can: split at the first one.
	const i = n.indexOf(' (')
	if (i > 0 && n.endsWith(')')) return { name: n.slice(i + 2, -1), machine: n.slice(0, i) }
	return { name: n, machine: '' }
}

/** The sources FeedView sees, plus the current one if it's offline (so it can still be chosen). */
function sourceList(state) {
	if (!state) return []
	const names = Array.isArray(state.sources) ? state.sources.slice() : []
	const cur = state.source && state.source.name
	if (cur && !names.includes(cur)) names.unshift(cur)
	return names
}

function sourceChoices(state) {
	return [{ id: NONE, label: NONE_LABEL }, ...sourceList(state).map((n) => ({ id: n, label: n }))]
}

/** Dropdown id -> FeedView source name ("" = None). */
function toSourceName(id) {
	return id === NONE || id === undefined || id === null ? '' : String(id)
}

/** FeedView source name -> dropdown id. */
function toSourceId(name) {
	return name ? name : NONE
}

function displayChoices(state) {
	const ds = state && Array.isArray(state.displays) ? state.displays : []
	if (!ds.length) return [1, 2, 3, 4].map((n) => ({ id: n, label: `Display ${n}` }))
	return ds.map((d) => ({ id: d.number, label: `${d.number}: ${d.name} (${d.w}x${d.h})` }))
}

function outputChoices(state) {
	const outs = state && state.audio && Array.isArray(state.audio.outputs) ? state.audio.outputs : []
	return outs.map((o) => ({ id: o.id, label: o.name }))
}

/** Changes when the dropdowns or presets have to be rebuilt. */
function choicesSignature(state) {
	return JSON.stringify([sourceList(state), displayChoices(state), outputChoices(state)])
}

module.exports = {
	NONE,
	NONE_LABEL,
	SWITCH,
	SETTINGS,
	TRANSITIONS,
	AUDIO_PAIRS,
	splitSourceName,
	sourceList,
	sourceChoices,
	toSourceName,
	toSourceId,
	displayChoices,
	outputChoices,
	choicesSignature,
}
