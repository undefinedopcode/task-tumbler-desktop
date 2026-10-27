import ReactMarkdown from 'react-markdown'
import remarkGfm from 'remark-gfm'

// A note's Markdown, rendered. No raw HTML; links open in the browser (the
// shell turns navigation away into that).
export function Markdown({ text }: { text: string }) {
  return (
    <div className="md">
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{text}</ReactMarkdown>
    </div>
  )
}

// Search snippets come HTML-escaped with matches in <b>: shown as text, the
// matches highlighted, nothing else interpreted.
export function Snippet({ html }: { html: string }) {
  const decode = (s: string) =>
    s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&')
  const parts = html.split(/(<b>.*?<\/b>)/g)
  return (
    <>
      {parts.map((p, i) =>
        p.startsWith('<b>') ? (
          <mark key={i} className="rounded-sm bg-warning-soft px-0.5 font-medium text-fg">
            {decode(p.slice(3, -4))}
          </mark>
        ) : (
          <span key={i}>{decode(p)}</span>
        ),
      )}
    </>
  )
}
