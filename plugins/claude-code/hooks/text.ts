// One-line labels for the monitor. Mirrors crates/agentsmon/src/adapters/tool.rs.

export const PROMPT_CHARS = 200
export const SUBJECT_CHARS = 160

// Collapses whitespace and cuts to `max` characters, marking the cut
export const preview = (text: unknown, max: number): string | undefined => {
  if (typeof text !== 'string') return undefined
  const line = text.split(/\s+/).filter(Boolean).join(' ')
  if (line === '') return undefined
  const chars = Array.from(line)
  return chars.length <= max ? line : chars.slice(0, max - 1).join('') + '…'
}

const FALLBACK_FIELDS = ['file_path', 'path', 'command', 'pattern', 'url', 'query', 'description']

// What a tool call is about: the command, the file, the pattern, the URL
export const subjectOf = (tool: string, input: unknown): string | undefined => {
  const fields = (input ?? {}) as Record<string, unknown>
  const field = (...names: string[]) => names.map(n => fields[n]).find(v => typeof v === 'string')
  let text: unknown
  switch (tool) {
    case 'Bash':
      text = field('command')
      break
    case 'Read':
    case 'Write':
    case 'Edit':
    case 'MultiEdit':
    case 'NotebookEdit':
      text = field('file_path', 'notebook_path')
      break
    case 'Grep':
    case 'Glob':
      text = field('pattern')
      break
    case 'WebFetch':
      text = field('url')
      break
    case 'WebSearch':
      text = field('query')
      break
    case 'Agent':
    case 'Task':
      text = field('description')
      break
    case 'Skill':
      text = field('skill')
      break
    case 'AskUserQuestion': {
      const questions = fields.questions
      text = Array.isArray(questions) ? (questions[0] as Record<string, unknown> | undefined)?.question : undefined
      break
    }
    default:
      text = tool.startsWith('mcp__') ? tool.split('__')[1] : field(...FALLBACK_FIELDS)
  }
  return preview(text, SUBJECT_CHARS)
}

export const basename = (path: string): string => {
  const parts = path.replace(/\/+$/, '').split('/')
  return parts[parts.length - 1] || path
}
