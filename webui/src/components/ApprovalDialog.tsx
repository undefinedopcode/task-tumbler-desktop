import { ShieldAlert } from 'lucide-react'
import { useCallback, useState } from 'react'
import { call, useSignal } from '../bridge/hooks'
import { Dialog } from './Dialog'
import { Button } from './ui'

// A launcher command runs only after this, once per exact command.
export function ApprovalDialog() {
  const [pending, setPending] = useState<{ actionId: string; taskTitle: string; command: string } | null>(null)
  useSignal(
    'app',
    'commandNeedsApproval',
    useCallback((actionId: string, taskTitle: string, command: string) => setPending({ actionId, taskTitle, command }), []),
  )
  return (
    <Dialog
      open={!!pending}
      onOpenChange={(o) => !o && setPending(null)}
      width="sm"
      title={
        <span className="flex items-center gap-2">
          <ShieldAlert className="size-5 text-warning" /> Run this command?
        </span>
      }
      description={pending ? `“${pending.taskTitle}” wants to run:` : ''}
      footer={
        <>
          <Button variant="ghost" onClick={() => setPending(null)}>
            Don't run
          </Button>
          <Button
            variant="primary"
            onClick={() => {
              if (pending) call('app', 'approveCommand', pending.actionId)
              setPending(null)
            }}
          >
            Allow and run
          </Button>
        </>
      }
    >
      <pre className="overflow-x-auto rounded-lg bg-hover p-3 font-mono text-[12.5px] whitespace-pre-wrap text-fg select-text">{pending?.command}</pre>
      <p className="mt-2 text-[12.5px] text-muted">It won't ask again for this exact command.</p>
    </Dialog>
  )
}
