import { useState, useEffect } from 'react';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertTitle, AlertDescription } from '../ui/alert';
import { Save, RotateCcw, CheckCircle, AlertTriangle } from 'lucide-react';
import type { BoardConfig } from './BoardConfig';

interface Corner {
    x: number;
    y: number;
}

interface Detection {
    id: string;
    corners: Corner[][];
    imageSize: [number, number];
}

interface CalibrationResult {
    camera_matrix: number[][];
    dist_coeffs: number[];
    reprojection_error: number;
}

interface ResultStepProps {
    cameraId: number;
    detections: Detection[];
    boardConfig: BoardConfig;
    onRestart: () => void;
}

export function ResultStep({
    cameraId,
    detections,
    boardConfig,
    onRestart
}: ResultStepProps) {
    const [isCalibrating, setIsCalibrating] = useState(true);
    const [isSaving, setIsSaving] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [result, setResult] = useState<CalibrationResult | null>(null);
    const [saveSuccess, setSaveSuccess] = useState(false);

    useEffect(() => {
        const performCalibration = async () => {
            try {
                const response = await fetch('/api/calibration/calibrate', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        frames: detections.map(d => ({
                            corners: d.corners
                        })),
                        image_width: detections[0].imageSize[0],
                        image_height: detections[0].imageSize[1],
                        squaresX: boardConfig.squaresX,
                        squaresY: boardConfig.squaresY,
                        square_length: boardConfig.squareLength,
                        marker_length: boardConfig.markerLength
                    })
                });

                const data = await response.json();
                if (!response.ok) throw new Error(data.error || "Calibration failed");

                setResult(data);
            } catch (e: unknown) {
                setError(e instanceof Error ? e.message : 'Calibration failed');
            } finally {
                setIsCalibrating(false);
            }
        };

        performCalibration();
    }, [detections, boardConfig]);

    const handleSave = async () => {
        if (!result) return;
        setIsSaving(true);
        try {
            const response = await fetch('/api/calibration/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    camera_id: cameraId,
                    camera_matrix: result.camera_matrix,
                    dist_coeffs: result.dist_coeffs,
                    reprojection_error: result.reprojection_error,
                    resolution: { width: detections[0].imageSize[0], height: detections[0].imageSize[1] }
                })
            });

            if (!response.ok) throw new Error("Failed to save calibration");

            setSaveSuccess(true);
        } catch (e: unknown) {
            setError(e instanceof Error ? e.message : 'Failed to save calibration');
        } finally {
            setIsSaving(false);
        }
    };

    if (isCalibrating) {
        return (
            <div className="flex flex-col items-center justify-center py-12 space-y-4">
                <div className="h-12 w-12 animate-spin rounded-full border-4 border-primary border-t-transparent"></div>
                <p className="text-lg font-medium">Calculating calibration parameters...</p>
                <p className="text-muted-foreground">This may take a moment.</p>
            </div>
        );
    }

    if (error || !result) {
        return (
            <div className="space-y-6">
                <Alert variant="destructive">
                    <AlertTriangle className="h-4 w-4" />
                    <AlertTitle>Calibration Failed</AlertTitle>
                    <AlertDescription>{error ?? 'No calibration result available'}</AlertDescription>
                </Alert>
                <Button onClick={onRestart} variant="outline">
                    <RotateCcw className="w-4 h-4 mr-2" />
                    Restart Wizard
                </Button>
            </div>
        );
    }

    return (
        <div className="space-y-6">
            <Card>
                <CardHeader>
                    <CardTitle>Calibration Results</CardTitle>
                    <CardDescription>
                        Review the calculated parameters before saving.
                    </CardDescription>
                </CardHeader>
                <CardContent className="space-y-6">
                    <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                        <div className="p-4 border rounded-lg bg-neutral-50 dark:bg-neutral-900">
                            <div className="text-sm text-muted-foreground mb-1">Reprojection Error</div>
                            <div className={`text-2xl font-bold ${result.reprojection_error < 1.0 ? 'text-green-600' : 'text-yellow-600'}`}>
                                {result.reprojection_error.toFixed(4)} px
                            </div>
                            <div className="text-xs text-muted-foreground mt-1">
                                Lower is better. Ideally &lt; 1.0.
                            </div>
                        </div>

                        <div className="p-4 border rounded-lg bg-neutral-50 dark:bg-neutral-900 col-span-2">
                            <div className="text-sm text-muted-foreground mb-1">Camera Matrix</div>
                            <pre className="text-xs font-mono overflow-x-auto p-2 bg-white dark:bg-black rounded border">
                                {JSON.stringify(result.camera_matrix, null, 2)}
                            </pre>
                        </div>
                    </div>

                    <div className="p-4 border rounded-lg bg-neutral-50 dark:bg-neutral-900">
                        <div className="text-sm text-muted-foreground mb-1">Distortion Coefficients</div>
                        <pre className="text-xs font-mono overflow-x-auto p-2 bg-white dark:bg-black rounded border">
                            {JSON.stringify(result.dist_coeffs, null, 2)}
                        </pre>
                    </div>
                </CardContent>
            </Card>

            <div className="flex justify-end gap-4">
                <Button variant="outline" onClick={onRestart}>
                    <RotateCcw className="w-4 h-4 mr-2" />
                    Discard & Restart
                </Button>
                <Button onClick={handleSave} disabled={isSaving || saveSuccess}>
                    {saveSuccess ? (
                        <>
                            <CheckCircle className="w-4 h-4 mr-2" />
                            Saved Successfully
                        </>
                    ) : (
                        <>
                            <Save className="w-4 h-4 mr-2" />
                            Save to Camera
                        </>
                    )}
                </Button>
            </div>
        </div>
    );
}
