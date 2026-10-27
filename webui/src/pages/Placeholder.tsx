import { Construction } from 'lucide-react'
import { Empty } from '../components/ui'

// Pages still to come across from the QML interface.
export function Placeholder({ title }: { title: string }) {
  return (
    <div className="mx-auto w-full max-w-[1080px] px-8 pt-8">
      <h1 className="mb-6 text-[26px] font-semibold tracking-tight">{title}</h1>
      <Empty icon={<Construction className="size-6" />} title="Not here yet" hint="This page is still being built for the new interface." />
    </div>
  )
}
