import { useState, useEffect } from 'react'
import { Settings as SettingsIcon, Network, Tag, HardDrive, AlertTriangle, HelpCircle } from 'lucide-react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Button } from '@/components/ui/button'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from '@/components/ui/tooltip'
import { toast } from '@/hooks/use-toast'
import { api } from '@/lib/api'

export default function Settings() {
  const [teamNumber, setTeamNumber] = useState('')
  const [hostname, setHostname] = useState('')
  const [ipMode, setIpMode] = useState<'dhcp' | 'static'>('dhcp')
  const [staticIp, setStaticIp] = useState('')
  const [gateway, setGateway] = useState('')
  const [subnetMask, setSubnetMask] = useState('255.255.255.0')
  const [networkInterface, setNetworkInterface] = useState('')
  const [networkInterfaces, setNetworkInterfaces] = useState<string[]>([])
  const [platform, setPlatform] = useState<'linux' | 'windows' | 'macos' | 'unknown'>('unknown')
  const [spinnakerAvailable, setSpinnakerAvailable] = useState(false)
  const [availableFields, setAvailableFields] = useState<{ name: string; is_system: boolean }[]>([])
  const [uploadingField, setUploadingField] = useState(false)
  const [selectedField, setSelectedField] = useState('')
  const [confirmAction, setConfirmAction] = useState<string | null>(null)
  const [isLoading, setIsLoading] = useState(false)
  const [hostnameError, setHostnameError] = useState('')

  // Helper to calculate static IP from team number
  const calculateStaticIP = (teamNum: number): string => {
    if (teamNum < 1 || teamNum > 99999) return '10.0.0.15'
    const padded = teamNum.toString().padStart(5, '0')
    const te = parseInt(padded.substring(0, 3), 10)
    const am = parseInt(padded.substring(3, 5), 10)
    return `10.${te}.${am}.15`
  }

  // Helper to calculate default gateway from team number
  const calculateDefaultGateway = (teamNum: number): string => {
    if (teamNum < 1 || teamNum > 99999) return '10.0.0.1'
    const padded = teamNum.toString().padStart(5, '0')
    const te = parseInt(padded.substring(0, 3), 10)
    const am = parseInt(padded.substring(3, 5), 10)
    return `10.${te}.${am}.1`
  }

  // Validate hostname
  const validateHostname = (value: string): string => {
    if (!value) return 'Hostname cannot be empty'
    if (value.length > 63) return 'Hostname must be 63 characters or less'
    if (!/^[a-zA-Z0-9]/.test(value)) return 'Hostname must start with a letter or number'
    if (!/[a-zA-Z0-9]$/.test(value)) return 'Hostname must end with a letter or number'
    if (!/^[a-zA-Z0-9-]+$/.test(value)) return 'Hostname can only contain letters, numbers, and hyphens'
    if (/--/.test(value)) return 'Hostname cannot contain consecutive hyphens'
    return ''
  }

  // Check if network settings are editable (Linux only)
  const isNetworkEditable = platform === 'linux'

  // Load settings on component mount
  useEffect(() => {
    const loadSettings = async () => {
      try {
        // Load platform first
        const platformData = await api.get<{ platform: 'linux' | 'windows' | 'macos' | 'unknown' }>('/api/system/platform')
        setPlatform(platformData.platform)

        // Load network interfaces
        const interfaces = await api.get<string[]>('/api/network/interfaces')
        setNetworkInterfaces(interfaces)

        // Load settings
        const data = await api.get<{
          global: {
            team_number: number
            hostname: string
            ip_mode: 'dhcp' | 'static'
            static_ip?: string
            gateway?: string
            subnet_mask?: string
            network_interface?: string
          }
          apriltag: {
            available_fields: { name: string; is_system: boolean }[]
            selected_field: string
          }
          network_tables: {
            port: number
            server_address: string
            table_name: string
          }
          spinnaker_available: boolean
        }>('/api/settings')

        const teamNum = data.global.team_number === 0 ? '' : String(data.global.team_number)
        setTeamNumber(teamNum)
        setHostname(data.global.hostname || '')
        setIpMode(data.global.ip_mode || 'dhcp')
        setSpinnakerAvailable(data.spinnaker_available || false)
        setSelectedField(data.apriltag.selected_field || '')
        setAvailableFields(data.apriltag.available_fields || [])

        // Load network-specific settings
        const parsedTeam = parseInt(teamNum, 10) || 0
        setStaticIp(data.global.static_ip || calculateStaticIP(parsedTeam))
        setGateway(data.global.gateway || calculateDefaultGateway(parsedTeam))
        setSubnetMask(data.global.subnet_mask || '255.255.255.0')

        // Set network interface - prefer saved value, then eth0, then first available
        if (data.global.network_interface) {
          setNetworkInterface(data.global.network_interface)
        } else if (interfaces.includes('eth0')) {
          setNetworkInterface('eth0')
        } else if (interfaces.length > 0) {
          setNetworkInterface(interfaces[0])
        }
      } catch (error) {
        console.error('Failed to load settings:', error)
        toast({
          variant: 'destructive',
          title: 'Error',
          description: 'Failed to load settings',
        })
      }
    }
    loadSettings()
  }, [])

  const handleSaveGlobal = async () => {
    // Validate team number
    if (teamNumber !== '') {
      const teamNum = parseInt(teamNumber, 10)
      if (isNaN(teamNum) || teamNum < 1 || teamNum > 99999) {
        toast({
          variant: 'destructive',
          title: 'Invalid Team Number',
          description: 'Team number must be between 1 and 99999',
        })
        return
      }
    }

    // Validate hostname on Linux
    if (isNetworkEditable && hostname) {
      const error = validateHostname(hostname)
      if (error) {
        setHostnameError(error)
        toast({
          variant: 'destructive',
          title: 'Invalid Hostname',
          description: error,
        })
        return
      }
    }

    setIsLoading(true)
    try {
      await api.put('/api/settings/global', {
        team_number: parseInt(teamNumber, 10) || 0,
        hostname: hostname,
        ip_mode: ipMode,
        static_ip: staticIp,
        gateway: gateway,
        subnet_mask: subnetMask,
        network_interface: networkInterface,
      })
      toast({
        title: 'Settings saved',
        description: isNetworkEditable && ipMode === 'static'
          ? 'Global settings updated. Network changes applied.'
          : 'Global settings updated successfully',
      })
      setHostnameError('')
    } catch (error: unknown) {
      const errorMessage = error instanceof Error ? error.message : 'Failed to save settings'
      toast({
        variant: 'destructive',
        title: 'Error',
        description: errorMessage,
      })
    } finally {
      setIsLoading(false)
    }
  }

  // Update static IP defaults when team number changes
  const handleTeamNumberChange = (value: string) => {
    setTeamNumber(value)
    const teamNum = parseInt(value, 10) || 0
    if (teamNum > 0) {
      setStaticIp(calculateStaticIP(teamNum))
      setGateway(calculateDefaultGateway(teamNum))
    }
  }

  // Validate hostname as user types
  const handleHostnameChange = (value: string) => {
    setHostname(value)
    if (isNetworkEditable && value) {
      setHostnameError(validateHostname(value))
    } else {
      setHostnameError('')
    }
  }

  const handleRefreshSpinnaker = async () => {
    setIsLoading(true)
    try {
      const data = await api.get<{ available: boolean }>('/api/spinnaker/status')
      setSpinnakerAvailable(data.available)
      toast({
        title: 'Spinnaker status refreshed',
        description: data.available ? 'SDK is available' : 'SDK not available',
      })
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to check Spinnaker status',
      })
    } finally {
      setIsLoading(false)
    }
  }

  const handleFieldChange = async (fieldName: string) => {
    setSelectedField(fieldName)
    try {
      await api.put('/api/settings/apriltag/select', {
        field: fieldName,
      })
      toast({
        title: 'Field layout updated',
        description: `Selected ${fieldName}`,
      })
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to save field selection',
      })
    }
  }

  const handleUploadField = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (!file) return

    // Reset input
    e.target.value = ''

    const reader = new FileReader()
    reader.onload = async (event) => {
      const content = event.target?.result as string
      try {
        // Validate JSON locally first
        JSON.parse(content)

        setUploadingField(true)
        await api.post('/api/apriltag/upload', {
          name: file.name.replace(/\.json$/i, ''), // Backend adds .json
          content: content,
        })

        // Refresh list
        const fields = await api.get<{ name: string; is_system: boolean }[]>('/api/settings/apriltag/fields')
        setAvailableFields(fields)

        toast({
          title: 'Field uploaded',
          description: `Successfully uploaded ${file.name}`,
        })
      } catch (error) {
        toast({
          variant: 'destructive',
          title: 'Error',
          description: 'Failed to upload field. Ensure it is a valid JSON file.',
        })
      } finally {
        setUploadingField(false)
      }
    }
    reader.readAsText(file)
  }

  const handleDeleteField = async (fieldName: string) => {
    if (!confirm(`Are you sure you want to delete ${fieldName}?`)) return

    try {
      await api.post('/api/apriltag/delete', {
        name: fieldName,
      })

      // Refresh list
      const fields = await api.get<{ name: string; is_system: boolean }[]>('/api/settings/apriltag/fields')
      setAvailableFields(fields)

      // If deleted field was selected, clear selection
      if (selectedField === fieldName) {
        setSelectedField('')
      }

      toast({
        title: 'Field deleted',
        description: `Successfully deleted ${fieldName}`,
      })
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to delete field',
      })
    }
  }

  const handleSystemAction = async (action: string) => {
    setConfirmAction(null)
    setIsLoading(true)

    try {
      switch (action) {
        case 'restart':
          await api.post('/api/control/restart-app')
          toast({
            title: 'Restarting',
            description: 'Application is restarting...',
          })
          break
        case 'reboot':
          await api.post('/api/control/reboot')
          toast({
            title: 'Rebooting',
            description: 'System is rebooting...',
          })
          break
        case 'factory-reset':
          await api.post('/api/settings/control/factory-reset')
          toast({
            title: 'Reset complete',
            description: 'All settings have been reset to defaults',
          })
          break
      }
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: `Failed to ${action}`,
      })
    } finally {
      setIsLoading(false)
    }
  }

  const handleExportDatabase = async () => {
    try {
      window.location.href = '/api/database/export'
      toast({
        title: 'Export started',
        description: 'Downloading database file...',
      })
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to export database',
      })
    }
  }

  const handleImportDatabase = async () => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.db'

    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0]
      if (!file) return

      setIsLoading(true)
      try {
        // Read file as ArrayBuffer and send as raw binary
        const arrayBuffer = await file.arrayBuffer()
        await api.post('/api/database/import', arrayBuffer, {
          headers: {
            'Content-Type': 'application/octet-stream',
          },
        })
        toast({
          title: 'Import successful',
          description: 'Database has been imported successfully',
        })
      } catch (error) {
        toast({
          variant: 'destructive',
          title: 'Error',
          description: 'Failed to import database',
        })
      } finally {
        setIsLoading(false)
      }
    }

    input.click()
  }

  return (
    <div className="p-6 space-y-6">
      <div>
        <h1 className="text-3xl font-semibold mb-2" data-testid="page-title-settings">
          Settings
        </h1>
        <p className="text-muted">Application and system configuration</p>
      </div>

      <Tabs defaultValue="global" className="space-y-4">
        <TabsList>
          <TabsTrigger value="global">
            <SettingsIcon className="h-4 w-4 mr-2" />
            Global
          </TabsTrigger>
          <TabsTrigger value="spinnaker">
            <Network className="h-4 w-4 mr-2" />
            Spinnaker
          </TabsTrigger>
          <TabsTrigger value="apriltag">
            <Tag className="h-4 w-4 mr-2" />
            AprilTag Fields
          </TabsTrigger>
          <TabsTrigger value="system">
            <HardDrive className="h-4 w-4 mr-2" />
            System
          </TabsTrigger>
        </TabsList>

        {/* Global Settings */}
        <TabsContent value="global" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle>Global Settings</CardTitle>
              <CardDescription>
                Configure team number, hostname, and network settings
              </CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="space-y-2">
                <Label htmlFor="team-number">Team Number</Label>
                <Input
                  id="team-number"
                  type="number"
                  min={1}
                  max={99999}
                  value={teamNumber}
                  onChange={(e) => handleTeamNumberChange(e.target.value)}
                  placeholder="Enter team number"
                />
              </div>

              <div className="space-y-2">
                <div className="flex items-center gap-2">
                  <Label htmlFor="hostname">Hostname</Label>
                  {!isNetworkEditable && (
                    <TooltipProvider>
                      <Tooltip>
                        <TooltipTrigger asChild>
                          <HelpCircle className="h-4 w-4 text-muted-foreground cursor-help" />
                        </TooltipTrigger>
                        <TooltipContent>
                          <p>This setting can only be modified through your operating system's network settings</p>
                        </TooltipContent>
                      </Tooltip>
                    </TooltipProvider>
                  )}
                </div>
                <Input
                  id="hostname"
                  value={hostname}
                  onChange={(e) => handleHostnameChange(e.target.value)}
                  placeholder="2852Vision-pi"
                  disabled={!isNetworkEditable}
                  className={hostnameError ? 'border-destructive' : ''}
                />
                {hostnameError && (
                  <p className="text-sm text-destructive">{hostnameError}</p>
                )}
                {isNetworkEditable && (
                  <p className="text-xs text-muted-foreground">
                    Hostname changes will take effect after a system reboot
                  </p>
                )}
              </div>

              <div className="space-y-2">
                <div className="flex items-center gap-2">
                  <Label htmlFor="ip-mode">IP Assignment Mode</Label>
                  {!isNetworkEditable && (
                    <TooltipProvider>
                      <Tooltip>
                        <TooltipTrigger asChild>
                          <HelpCircle className="h-4 w-4 text-muted-foreground cursor-help" />
                        </TooltipTrigger>
                        <TooltipContent>
                          <p>This setting can only be modified through your operating system's network settings</p>
                        </TooltipContent>
                      </Tooltip>
                    </TooltipProvider>
                  )}
                </div>
                <Select
                  value={ipMode}
                  onValueChange={(v) => setIpMode(v as 'dhcp' | 'static')}
                  disabled={!isNetworkEditable}
                >
                  <SelectTrigger id="ip-mode">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="dhcp">DHCP</SelectItem>
                    <SelectItem value="static">Static IP</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              {/* Network Interface - only show on Linux */}
              {isNetworkEditable && (
                <div className="space-y-2">
                  <Label htmlFor="network-interface">Network Interface</Label>
                  <Select value={networkInterface} onValueChange={setNetworkInterface}>
                    <SelectTrigger id="network-interface">
                      <SelectValue placeholder="Select network interface" />
                    </SelectTrigger>
                    <SelectContent>
                      {networkInterfaces.map((iface) => (
                        <SelectItem key={iface} value={iface}>
                          {iface}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </div>
              )}

              {/* Static IP settings - only show when static mode selected and on Linux */}
              {isNetworkEditable && ipMode === 'static' && (
                <div className="space-y-4 p-4 border rounded-md bg-muted/50">
                  <h4 className="font-medium">Static IP Configuration</h4>

                  <div className="space-y-2">
                    <Label htmlFor="static-ip">Static IP Address</Label>
                    <Input
                      id="static-ip"
                      value={staticIp}
                      onChange={(e) => setStaticIp(e.target.value)}
                      placeholder="10.TE.AM.15"
                    />
                    <p className="text-xs text-muted-foreground">
                      Based on team number: 10.TE.AM.15
                    </p>
                  </div>

                  <div className="space-y-2">
                    <Label htmlFor="gateway">Gateway</Label>
                    <Input
                      id="gateway"
                      value={gateway}
                      onChange={(e) => setGateway(e.target.value)}
                      placeholder="10.TE.AM.1"
                    />
                  </div>

                  <div className="space-y-2">
                    <Label htmlFor="subnet-mask">Subnet Mask</Label>
                    <Input
                      id="subnet-mask"
                      value={subnetMask}
                      onChange={(e) => setSubnetMask(e.target.value)}
                      placeholder="255.255.255.0"
                    />
                  </div>
                </div>
              )}

              <Button onClick={handleSaveGlobal} disabled={isLoading}>
                {isLoading ? 'Saving...' : 'Save Global Settings'}
              </Button>
            </CardContent>
          </Card>
        </TabsContent>

        {/* Spinnaker Settings */}
        <TabsContent value="spinnaker" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle>Spinnaker SDK</CardTitle>
              <CardDescription>
                FLIR Spinnaker SDK for industrial cameras (Chameleon3, Blackfly, etc.)
              </CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="space-y-2">
                <Label>SDK Status</Label>
                <div className={`p-3 rounded-md border ${spinnakerAvailable ? 'bg-green-50 border-green-200 dark:bg-green-950 dark:border-green-800' : 'bg-yellow-50 border-yellow-200 dark:bg-yellow-950 dark:border-yellow-800'}`}>
                  <p className={`text-sm font-medium ${spinnakerAvailable ? 'text-green-700 dark:text-green-300' : 'text-yellow-700 dark:text-yellow-300'}`}>
                    {spinnakerAvailable ? 'Spinnaker SDK is available' : 'Spinnaker SDK not available'}
                  </p>
                  <p className="text-xs text-muted mt-1">
                    {spinnakerAvailable
                      ? 'FLIR cameras can be discovered and used'
                      : 'Rebuild backend with --spinnaker=y to enable FLIR camera support'}
                  </p>
                </div>
              </div>

              <div className="space-y-2">
                <Label>Supported Cameras</Label>
                <ul className="text-sm text-muted list-disc list-inside space-y-1">
                  <li>Teledyne FLIR Chameleon3</li>
                  <li>Teledyne FLIR Blackfly</li>
                  <li>Other Spinnaker-compatible USB3 Vision cameras</li>
                </ul>
              </div>

              <Button variant="outline" onClick={handleRefreshSpinnaker} disabled={isLoading}>
                {isLoading ? 'Checking...' : 'Refresh Status'}
              </Button>
            </CardContent>
          </Card>
        </TabsContent>

        {/* AprilTag Fields */}
        <TabsContent value="apriltag" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle>AprilTag Field Layouts</CardTitle>
              <CardDescription>
                Manage FRC field layouts for AprilTag pose estimation
              </CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="space-y-2">
                <Label htmlFor="field-select">Active Field Layout</Label>
                <Select value={selectedField} onValueChange={handleFieldChange}>
                  <SelectTrigger id="field-select">
                    <SelectValue placeholder="Select field layout" />
                  </SelectTrigger>
                  <SelectContent>
                    {availableFields.map((field) => (
                      <SelectItem key={field.name} value={field.name}>
                        {field.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="space-y-2">
                <Label>Manage Fields</Label>
                <div className="border rounded-md divide-y">
                  {availableFields.filter(f => !f.is_system).map((field) => (
                    <div key={field.name} className="flex items-center justify-between p-3">
                      <div className="flex items-center gap-2">
                        <span className="text-sm font-medium">{field.name}</span>
                      </div>
                      <Button
                        variant="ghost"
                        size="sm"
                        className="text-destructive hover:text-destructive"
                        onClick={() => handleDeleteField(field.name)}
                      >
                        Delete
                      </Button>
                    </div>
                  ))}
                  {availableFields.filter(f => !f.is_system).length === 0 && (
                    <div className="p-3 text-sm text-muted text-center">
                      No custom fields uploaded
                    </div>
                  )}
                </div>
              </div>

              <div className="space-y-2">
                <Label>Upload Custom Field</Label>
                <Input
                  type="file"
                  accept=".json"
                  onChange={handleUploadField}
                  disabled={uploadingField}
                />
                <p className="text-xs text-muted">
                  Upload a JSON file with custom AprilTag field layout
                </p>
              </div>
            </CardContent>
          </Card>
        </TabsContent>

        {/* System Controls */}
        <TabsContent value="system" className="space-y-4">
          <Card>
            <CardHeader>
              <CardTitle>System Controls</CardTitle>
              <CardDescription>
                Manage application lifecycle and database
              </CardDescription>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                <Button variant="outline" onClick={() => setConfirmAction('restart')}>
                  Restart Application
                </Button>
                <Button variant="outline" onClick={() => setConfirmAction('reboot')}>
                  Reboot Device
                </Button>
                <Button variant="outline" onClick={handleExportDatabase}>
                  Export Database
                </Button>
                <Button variant="outline" onClick={handleImportDatabase}>
                  Import Database
                </Button>
              </div>

              <div className="pt-4 border-t">
                <Button
                  variant="destructive"
                  onClick={() => setConfirmAction('factory-reset')}
                  className="w-full"
                >
                  Factory Reset
                </Button>
                <p className="text-xs text-muted mt-2 text-center">
                  Warning: This will erase all cameras, pipelines, and settings
                </p>
              </div>
            </CardContent>
          </Card>
        </TabsContent>
      </Tabs>

      {/* Confirmation Dialog */}
      <Dialog open={confirmAction !== null} onOpenChange={() => setConfirmAction(null)}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle className="flex items-center gap-2 text-[var(--color-warning)]">
              <AlertTriangle className="h-5 w-5" />
              Confirm Action
            </DialogTitle>
            <DialogDescription>
              {confirmAction === 'restart' && 'Are you sure you want to restart the application? This will temporarily interrupt all camera feeds and pipelines.'}
              {confirmAction === 'reboot' && 'Are you sure you want to reboot the device? All processes will be stopped.'}
              {confirmAction === 'factory-reset' && 'Are you sure you want to reset all settings to factory defaults? This will erase all cameras, pipelines, and configuration. This action cannot be undone.'}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" onClick={() => setConfirmAction(null)}>
              Cancel
            </Button>
            <Button
              variant={confirmAction === 'factory-reset' ? 'destructive' : 'default'}
              onClick={() => confirmAction && handleSystemAction(confirmAction)}
            >
              Confirm
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}
