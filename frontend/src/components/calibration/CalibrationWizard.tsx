import { useState, useEffect, useCallback } from 'react';
import { api } from '@/lib/api';
import type { Camera } from '@/types';
import { useToast } from '@/hooks/use-toast';

import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Label } from '../ui/label';
import { BoardConfigStep, type BoardConfig } from './BoardConfig';
import { CaptureStep } from './CaptureStep';
import { ResultStep } from './ResultStep';
import { Check, Loader2 } from 'lucide-react';

interface Corner {
    id: number;
    x: number;
    y: number;
}

// Detection type matches CaptureStep's Detection interface
interface Detection {
    id: string;
    image: string;
    debugImage?: string;
    corners: Corner[][];
    imageSize: [number, number];
}

// Fallback for Steps component if not available, using a simple custom one
const WizardSteps = ({ currentStep, steps }: { currentStep: number, steps: string[] }) => {
    return (
        <div className="flex items-center justify-center mb-8">
            {steps.map((step, idx) => (
                <div key={idx} className="flex items-center">
                    <div className={`flex items-center justify-center w-8 h-8 rounded-full border-2 
            ${idx < currentStep ? 'bg-primary border-primary text-primary-foreground' :
                            idx === currentStep ? 'border-primary text-primary' : 'border-muted text-muted-foreground'}`}>
                        {idx < currentStep ? <Check className="w-4 h-4" /> : idx + 1}
                    </div>
                    <span className={`ml-2 text-sm font-medium ${idx === currentStep ? 'text-foreground' : 'text-muted-foreground'}`}>
                        {step}
                    </span>
                    {idx < steps.length - 1 && (
                        <div className="w-12 h-px bg-border mx-4" />
                    )}
                </div>
            ))}
        </div>
    );
};

export function CalibrationWizard() {
    const { toast } = useToast();
    const [step, setStep] = useState(0);
    const [cameras, setCameras] = useState<Camera[]>([]);
    const [selectedCameraId, setSelectedCameraId] = useState<string>('');
    const [loading, setLoading] = useState(true);

    const [boardConfig, setBoardConfig] = useState<BoardConfig>({
        squaresX: 5,
        squaresY: 7,
        squareLength: 0.034,
        markerLength: 0.025,
        dictionary: 'DICT_6X6_50'
    });

    const [detections, setDetections] = useState<Detection[]>([]);

    const loadCameras = useCallback(async () => {
        setLoading(true);
        try {
            const data = await api.get<Camera[]>('/api/cameras');
            setCameras(data);
            if (data.length > 0) {
                // Auto-select if only one camera or if none selected yet
                if (data.length === 1 || !selectedCameraId) {
                    setSelectedCameraId(data[0].id.toString());
                }
            }
        } catch (error) {
            const message = error instanceof Error ? error.message : 'Failed to load cameras';
            toast({ title: 'Error', description: message, variant: 'destructive' });
        } finally {
            setLoading(false);
        }
    }, [selectedCameraId, toast]);

    useEffect(() => {
        loadCameras();
    }, []); // eslint-disable-line react-hooks/exhaustive-deps

    const selectedCamera = cameras.find(c => c.id.toString() === selectedCameraId);

    const handleNext = () => setStep(s => s + 1);
    const handleRestart = () => {
        setStep(0);
        setDetections([]);
    };

    return (
        <div className="max-w-5xl mx-auto p-6">
            <div className="mb-8 text-center">
                <h1 className="text-3xl font-bold tracking-tight mb-2">Camera Calibration</h1>
                <p className="text-muted-foreground">
                    Calibrate your cameras to improve vision accuracy using a Charuco board.
                </p>
            </div>

            <WizardSteps
                currentStep={step}
                steps={['Setup', 'Capture', 'Results']}
            />

            {step === 0 && (
                <div className="space-y-6">
                    <Card>
                        <CardHeader>
                            <CardTitle>Camera Selection</CardTitle>
                        </CardHeader>
                        <CardContent>
                            <div className="space-y-2">
                                <Label>Select Camera</Label>
                                {loading ? (
                                    <div className="flex items-center gap-2 text-muted-foreground">
                                        <Loader2 className="h-4 w-4 animate-spin" />
                                        Loading cameras...
                                    </div>
                                ) : (
                                    <Select value={selectedCameraId} onValueChange={setSelectedCameraId}>
                                        <SelectTrigger>
                                            <SelectValue placeholder={cameras.length ? "Select a camera" : "No cameras available"} />
                                        </SelectTrigger>
                                        <SelectContent>
                                            {cameras.map(cam => (
                                                <SelectItem key={cam.id} value={cam.id.toString()}>
                                                    {cam.name} ({cam.identifier})
                                                </SelectItem>
                                            ))}
                                        </SelectContent>
                                    </Select>
                                )}
                            </div>
                        </CardContent>
                    </Card>

                    <BoardConfigStep
                        config={boardConfig}
                        onChange={setBoardConfig}
                        onNext={handleNext}
                        nextDisabled={!selectedCameraId}
                    />
                </div>
            )}

            {step === 1 && selectedCamera && (
                <CaptureStep
                    cameraId={selectedCamera.id}
                    boardConfig={boardConfig}
                    onDetectionsChange={setDetections}
                    onNext={handleNext}
                    onUpdateSquareSize={(size) => setBoardConfig(c => ({ ...c, squareLength: size }))}
                />
            )}

            {step === 2 && selectedCamera && (
                <ResultStep
                    cameraId={selectedCamera.id}
                    detections={detections}
                    boardConfig={boardConfig}
                    onRestart={handleRestart}
                />
            )}
        </div>
    );
}
