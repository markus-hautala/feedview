// Which of your requests pass: collects the test scenarios' results from a ctest JUnit file
// (ctest --output-junit), groups them by the [REQ-xx] tags in their names and writes a report
// (Markdown) next to it. Every request in tests/REQUIREMENTS.md gets one status:
//   PASS        its scenarios ran and passed
//   FAIL        one of its scenarios failed
//   SKIPPED     its scenarios couldn't run on this computer (e.g. no NDI runtime)
//   NOT TESTED  no scenario checked it in this run
//
//   node scripts/requirements-report.mjs build/test-results.xml [--strict] [--out report.md]
//
// Exit code 1 if a request failed, or, with --strict, if any request didn't pass (skipped or
// not tested count as not passing: that's how the full run and Claude's checks use it).
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..')
const args = process.argv.slice(2)
const strict = args.includes('--strict')
const junitPath = args.find((a) => !a.startsWith('--')) || path.join(root, 'build', 'test-results.xml')
const outIdx = args.indexOf('--out')
const outPath = outIdx >= 0 ? args[outIdx + 1] : path.join(path.dirname(junitPath), 'requirements-report.md')

// ---- Requests
const catalog = fs
	.readFileSync(path.join(root, 'tests', 'REQUIREMENTS.md'), 'utf8')
	.split(/\r?\n/)
	.map((l) => l.match(/^\| (REQ-\d+) \| (.*?) \| (.*?) \|$/))
	.filter(Boolean)
	.map((m) => ({ id: m[1], request: m[2], checkedBy: m[3], pass: [], fail: [], skip: [] }))
const byId = Object.fromEntries(catalog.map((r) => [r.id, r]))

// ---- Results
if (!fs.existsSync(junitPath)) {
	console.error(`No test results at ${junitPath} (run the tests with ctest --output-junit)`)
	process.exit(1)
}
const xml = fs.readFileSync(junitPath, 'utf8')
const unescape = (s) =>
	s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&amp;/g, '&')
const testSets = []
for (const m of xml.matchAll(/<testcase\b([^>]*)>([\s\S]*?)<\/testcase>/g)) {
	const name = (m[1].match(/\bname="([^"]*)"/) || [])[1]
	const status = (m[1].match(/\bstatus="([^"]*)"/) || [])[1]
	const out = unescape((m[2].match(/<system-out>([\s\S]*?)<\/system-out>/) || [])[1] || '')
	testSets.push({ name, status })
	for (const line of out.split(/\r?\n/)) {
		let result, scenario
		const r = line.match(/^RESULT (PASS|FAIL|SKIP) (.*)$/) // C++ tests, coverage check
		const tap = line.match(/^(not ok|ok) \d+ - (.*?)(?: # (SKIP|TODO)\b.*)?$/) // Node tests (TAP)
		if (r) [result, scenario] = [r[1], r[2]]
		else if (tap) [result, scenario] = [tap[1] === 'not ok' ? 'FAIL' : tap[3] ? 'SKIP' : 'PASS', tap[2]]
		else continue
		for (const t of scenario.matchAll(/\[(REQ-\d+)\]/g)) {
			const req = byId[t[1]]
			if (!req) continue
			const entry = `${name}: ${scenario.replace(/\s*\[REQ-\d+\]/g, '').trim()}`
			req[result === 'PASS' ? 'pass' : result === 'FAIL' ? 'fail' : 'skip'].push(entry)
		}
	}
}

// ---- Verdicts
for (const r of catalog) {
	r.status = r.fail.length ? 'FAIL' : r.pass.length ? 'PASS' : r.skip.length ? 'SKIPPED' : 'NOT TESTED'
}
const passed = catalog.filter((r) => r.status === 'PASS').length
const failedSets = testSets.filter((t) => t.status === 'fail').map((t) => t.name)
const notRunSets = testSets.filter((t) => t.status !== 'run' && t.status !== 'fail').map((t) => t.name)
const ok = strict ? passed === catalog.length && failedSets.length === 0 : catalog.every((r) => r.status !== 'FAIL')

// ---- Console
const pad = (s, n) => (s.length > n ? s.slice(0, n - 1) + '…' : s.padEnd(n))
console.log('')
console.log('Your requests (tests/REQUIREMENTS.md):')
for (const r of catalog) {
	const counts = `${r.pass.length} passed${r.fail.length ? `, ${r.fail.length} FAILED` : ''}${r.skip.length ? `, ${r.skip.length} skipped` : ''}`
	console.log(`  ${r.id}  ${pad(r.status, 10)}  ${pad(r.request.replace(/^"|"$/g, ''), 70)}  ${counts}`)
	for (const f of r.fail) console.log(`          FAILED: ${f}`)
}
if (failedSets.length) console.log(`Test sets that failed: ${failedSets.join(', ')}`)
if (notRunSets.length) console.log(`Test sets that didn't run: ${notRunSets.join(', ')}`)
console.log(
	ok && passed === catalog.length
		? `All ${catalog.length} of your requests pass.`
		: `${passed} of ${catalog.length} requests pass${strict ? '' : ' (desktop tests left out: not every request can be checked)'}.`,
)
console.log(`Report: ${outPath}`)

// ---- Report file
const d = new Date()
const two = (v) => String(v).padStart(2, '0')
const now = `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())} ${two(d.getHours())}:${two(d.getMinutes())}:${two(d.getSeconds())}`
let md = `# Your requests: test report\n\n${now} — ${passed} of ${catalog.length} requests pass`
md += failedSets.length ? `; failed test sets: ${failedSets.join(', ')}` : ''
md += `.\n\n| ID | Status | Your request | Scenarios |\n|---|---|---|---|\n`
for (const r of catalog)
	md += `| ${r.id} | **${r.status}** | ${r.request} | ${r.pass.length} passed, ${r.fail.length} failed, ${r.skip.length} skipped |\n`
for (const r of catalog) {
	md += `\n## ${r.id}: ${r.status}\n\n${r.request}\n\n*Checked by:* ${r.checkedBy}\n\n`
	for (const [label, list] of [
		['Failed', r.fail],
		['Passed', r.pass],
		['Skipped', r.skip],
	])
		if (list.length) md += `${label}:\n${list.map((x) => `- ${x}`).join('\n')}\n\n`
}
fs.writeFileSync(outPath, md)
process.exit(ok ? 0 : 1)
