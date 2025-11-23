import { useState, useRef, useEffect } from 'react';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Button } from '../ui/button';
import { Input } from '../ui/input';
import { Label } from '../ui/label';
import { ScrollArea } from '../ui/scroll-area';
import { Trash2, Camera as CameraIcon, CheckCircle, AlertCircle, HelpCircle } from 'lucide-react';
import type { BoardConfig } from './BoardConfig';
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from '../ui/tooltip';

interface Detection {
    id: string;
    image: string; // base64
    debugImage?: string; // base64 with corners drawn
    charucoCorners: any[];
    charucoIds: any[];
    imageSize: [number, number];
}

interface CaptureStepProps {
    cameraId: number;
    boardConfig: BoardConfig;
    onDetectionsChange: (detections: Detection[]) => void;
    onNext: () => void;
    onUpdateSquareSize: (size: number) => void;
}

const METERS_TO_INCHES = 39.3701;
const INCHES_TO_METERS = 0.0254;

export function CaptureStep({
    cameraId,
    boardConfig,
    onDetectionsChange,
    onNext,
    onUpdateSquareSize
}: CaptureStepProps) {
    const [detections, setDetections] = useState<Detection[]>([]);
    const [isCapturing, setIsCapturing] = useState(false);
    const [lastError, setLastError] = useState<string | null>(null);
    const imgRef = useRef<HTMLImageElement>(null);
    const canvasRef = useRef<HTMLCanvasElement>(null);

    const [squareLengthInch, setSquareLengthInch] = useState((boardConfig.squareLength * METERS_TO_INCHES).toFixed(3));

    // Sync local inch state when config changes externally
    useEffect(() => {
        setSquareLengthInch((boardConfig.squareLength * METERS_TO_INCHES).toFixed(3));
    }, [boardConfig.squareLength]);

    // Use the video feed endpoint with the camera's numeric ID
    const streamUrl = `/api/video_feed/${cameraId}`;

    const handleCapture = async () => {
        if (!imgRef.current || !canvasRef.current) return;

        setIsCapturing(true);
        setLastError(null);

        try {
            const img = imgRef.current;
            const canvas = canvasRef.current;
            canvas.width = img.naturalWidth;
            canvas.height = img.naturalHeight;

            const ctx = canvas.getContext('2d');
            if (!ctx) throw new Error("Could not get canvas context");

            // Draw the current frame to canvas
            ctx.drawImage(img, 0, 0);

            const dataUrl = canvas.toDataURL('image/jpeg');

            // Send to backend for detection
            const response = await fetch('/api/calibration/detect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    image: dataUrl,
                    ...boardConfig
                })
            });

            const result = await response.json();

            if (!response.ok) {
                throw new Error(result.error || "Detection failed");
            }

            if (result.detected) {
                const newDetection: Detection = {
                    id: Date.now().toString(),
                    image: dataUrl,
                    debugImage: result.debugImage,
                    charucoCorners: result.detectionData.charucoCorners,
                    charucoIds: result.detectionData.charucoIds,
                    imageSize: result.imageSize
                };

                const newDetections = [...detections, newDetection];
                setDetections(newDetections);
                onDetectionsChange(newDetections);
            } else {
                setLastError("No board detected. Please adjust the board or camera.");
            }

        } catch (e: any) {
            setLastError(e.message);
        } finally {
            setIsCapturing(false);
        }
    };

    const handleDelete = (id: string) => {
        const newDetections = detections.filter(d => d.id !== id);
        setDetections(newDetections);
        onDetectionsChange(newDetections);
    };

    const handleSquareSizeChange = (val: string) => {
        setSquareLengthInch(val);
        const inches = parseFloat(val);
        if (!isNaN(inches)) {
            onUpdateSquareSize(inches * INCHES_TO_METERS);
        }
    };

    return (
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <div className="lg:col-span-2 space-y-4">
                <Card>
                    <CardHeader>
                        <CardTitle>Capture Frames</CardTitle>
                        <CardDescription>
                            Hold the board in different positions and angles. Aim for at least 10-15 varied captures.
                        </CardDescription>
                    </CardHeader>
                    <CardContent>
                        <div className="relative aspect-video bg-black rounded-lg overflow-hidden mb-4">
                            <img
                                ref={imgRef}
                                src={streamUrl}
                                alt="Camera Stream"
                                crossOrigin="anonymous"
                                className="w-full h-full object-contain"
                            />
                            {/* Hidden canvas for capture */}
                            <canvas ref={canvasRef} className="hidden" />
                        </div>

                        <div className="flex items-center justify-between gap-4">
                            <div className="flex-1">
                                {lastError && (
                                    <div className="text-red-500 text-sm flex items-center">
                                        <AlertCircle className="w-4 h-4 mr-1" />
                                        {lastError}
                                    </div>
                                )}
                                {!lastError && detections.length > 0 && (
                                    <div className="text-green-500 text-sm flex items-center">
                                        <CheckCircle className="w-4 h-4 mr-1" />
                                        Last capture successful
                                    </div>
                                )}
                            </div>
                            <Button onClick={handleCapture} disabled={isCapturing} size="lg">
                                <CameraIcon className="w-5 h-5 mr-2" />
                                {isCapturing ? 'Processing...' : 'Capture Frame'}
                            </Button>
                        </div>
                    </CardContent>
                </Card>

                <Card>
                    <CardHeader>
                        <CardTitle>Adjustments</CardTitle>
                    </CardHeader>
                    <CardContent>
                        <div className="grid grid-cols-2 gap-4">
                            <div className="space-y-2">
                                <div className="flex items-center gap-2">
                                    <Label>Measured Square Size (in)</Label>
                                    <TooltipProvider>
                                        <Tooltip>
                                            <TooltipTrigger>
                                                <HelpCircle className="w-4 h-4 text-muted-foreground" />
                                            </TooltipTrigger>
                                            <TooltipContent>
                                                <p className="max-w-xs">
                                                    If using a screen, measure the actual size of the black squares with a ruler and enter it here.
                                                </p>
                                            </TooltipContent>
                                        </Tooltip>
                                    </TooltipProvider>
                                </div>
                                <Input
                                    type="number"
                                    step="0.01"
                                    value={squareLengthInch}
                                    onChange={(e) => handleSquareSizeChange(e.target.value)}
                                />
                            </div>
                        </div>
                    </CardContent>
                </Card>
            </div>

            <div className="lg:col-span-1">
                <Card className="h-full flex flex-col">
                    <CardHeader>
                        <CardTitle>Captures ({detections.length})</CardTitle>
                    </CardHeader>
                    <CardContent className="flex-1 min-h-0">
                        <ScrollArea className="h-[500px] pr-4">
                            <div className="space-y-4">
                                {detections.map((detection, idx) => (
                                    <div key={detection.id} className="relative group border rounded-lg overflow-hidden">
                                        <img
                                            src={detection.debugImage || detection.image}
                                            alt={`Capture ${idx + 1}`}
                                            className="w-full h-auto"
                                        />
                                        <div className="absolute top-2 right-2 opacity-0 group-hover:opacity-100 transition-opacity">
                                            <Button
                                                variant="destructive"
                                                size="icon"
                                                onClick={() => handleDelete(detection.id)}
                                            >
                                                <Trash2 className="w-4 h-4" />
                                            </Button>
                                        </div>
                                        <div className="absolute bottom-0 left-0 right-0 bg-black/50 text-white text-xs p-1">
                                            Corners: {detection.charucoCorners.length}
                                        </div>
                                    </div>
                                ))}
                                {detections.length === 0 && (
                                    <div className="text-center text-muted-foreground py-8">
                                        No frames captured yet.
                                    </div>
                                )}
                            </div>
                        </ScrollArea>
                    </CardContent>
                    <div className="p-6 pt-0">
                        <Button
                            className="w-full"
                            disabled={detections.length < 5}
                            onClick={onNext}
                        >
                            Calibrate ({detections.length} frames)
                        </Button>
                        {detections.length < 5 && (
                            <p className="text-xs text-center mt-2 text-muted-foreground">
                                Capture at least 5 frames (10+ recommended)
                            </p>
                        )}
                    </div>
                </Card>
            </div>
        </div>
    );
}
