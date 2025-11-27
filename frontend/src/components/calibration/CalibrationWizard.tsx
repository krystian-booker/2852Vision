import { useState, useEffect } from 'react';

import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Label } from '../ui/label';
import { BoardConfigStep, type BoardConfig } from './BoardConfig';
import { CaptureStep } from './CaptureStep';
import { ResultStep } from './ResultStep';
import { Check } from 'lucide-react';

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
    const [step, setStep] = useState(0);
    const [cameras, setCameras] = useState<any[]>([]);
    const [selectedCameraId, setSelectedCameraId] = useState<string>('');

    const [boardConfig, setBoardConfig] = useState<BoardConfig>({
        squaresX: 5,
        squaresY: 7,
        squareLength: 0.034,
        markerLength: 0.025,
        dictionary: 'DICT_6X6_50'
    });

    const [detections, setDetections] = useState<any[]>([]);

    useEffect(() => {
        fetch('/api/cameras')
            .then(res => res.json())
            .then(data => {
                setCameras(data);
                if (data.length > 0) {
                    // Auto-select if only one camera or if none selected yet
                    if (data.length === 1 || !selectedCameraId) {
                        setSelectedCameraId(data[0].id.toString());
                    }
                }
            })
            .catch(err => console.error("Failed to load cameras", err));
    }, []);

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
                                <Select value={selectedCameraId} onValueChange={setSelectedCameraId}>
                                    <SelectTrigger>
                                        <SelectValue placeholder="Select a camera" />
                                    </SelectTrigger>
                                    <SelectContent>
                                        {cameras.map(cam => (
                                            <SelectItem key={cam.id} value={cam.id.toString()}>
                                                {cam.name} ({cam.identifier})
                                            </SelectItem>
                                        ))}
                                    </SelectContent>
                                </Select>
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
