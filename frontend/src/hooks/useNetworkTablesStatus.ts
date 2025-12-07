import { useVisionSubscription } from './useVisionWebSocket'

export interface NTStatus {
  connected: boolean
  serverAddress: string
  teamNumber: number
  mode: 'client' | 'server' | 'disconnected'
}

const DEFAULT_STATUS: NTStatus = {
  connected: false,
  serverAddress: '',
  teamNumber: 0,
  mode: 'disconnected'
}

export function useNetworkTablesStatus() {
  const status = useVisionSubscription<NTStatus>('nt_status')
  return { status: status ?? DEFAULT_STATUS }
}
