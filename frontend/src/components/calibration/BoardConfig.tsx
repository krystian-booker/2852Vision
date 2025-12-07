import { useState, useEffect } from 'react';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Label } from '../ui/label';
import { Input } from '../ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Button } from '../ui/button';
import { Download, HelpCircle } from 'lucide-react';
import { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from '../ui/tooltip';

export interface BoardConfig {
  squaresX: number;
  squaresY: number;
  squareLength: number; // meters
  markerLength: number; // meters
  dictionary: string;
  pageWidth?: number;  // mm, for PDF generation (paper presets only)
  pageHeight?: number; // mm, for PDF generation (paper presets only)
}

interface BoardConfigProps {
  config: BoardConfig;
  onChange: (config: BoardConfig) => void;
  onNext: () => void;
  nextDisabled?: boolean;
}

const METERS_TO_INCHES = 39.3701;
const INCHES_TO_METERS = 0.0254;

const PRESETS = {
  A4: { squaresX: 5, squaresY: 7, squareLength: 0.034, markerLength: 0.025, pageWidth: 210, pageHeight: 297 },
  A3: { squaresX: 7, squaresY: 10, squareLength: 0.034, markerLength: 0.025, pageWidth: 297, pageHeight: 420 },
  Letter: { squaresX: 5, squaresY: 7, squareLength: 0.035, markerLength: 0.026, pageWidth: 215.9, pageHeight: 279.4 },
  Screen: { squaresX: 5, squaresY: 7, squareLength: 0.040, markerLength: 0.030 },
};

export function BoardConfigStep({ config, onChange, onNext, nextDisabled }: BoardConfigProps) {
  const [preset, setPreset] = useState('A4');
  const [previewUrl, setPreviewUrl] = useState('');

  // Local state for inch values to avoid floating point jitter during typing
  const [squareLengthInch, setSquareLengthInch] = useState((config.squareLength * METERS_TO_INCHES).toFixed(3));
  const [markerLengthInch, setMarkerLengthInch] = useState((config.markerLength * METERS_TO_INCHES).toFixed(3));

  useEffect(() => {
    // Update preview URL whenever config changes
    const params = new URLSearchParams({
      squaresX: config.squaresX.toString(),
      squaresY: config.squaresY.toString(),
      squareLength: config.squareLength.toString(),
      markerLength: config.markerLength.toString(),
      dictionary: config.dictionary,
    });
    setPreviewUrl(`/api/calibration/board?${params.toString()}`);
  }, [config]);

  // Sync local inch state when config changes externally (e.g. preset change)
  useEffect(() => {
    setSquareLengthInch((config.squareLength * METERS_TO_INCHES).toFixed(3));
    setMarkerLengthInch((config.markerLength * METERS_TO_INCHES).toFixed(3));
  }, [config.squareLength, config.markerLength]);

  const handlePresetChange = (value: string) => {
    setPreset(value);
    if (value in PRESETS) {
      const p = PRESETS[value as keyof typeof PRESETS];
      onChange({ ...config, ...p });
    }
  };

  const handleSquareLengthChange = (val: string) => {
    setSquareLengthInch(val);
    const inches = parseFloat(val);
    if (!isNaN(inches)) {
      setPreset('Custom');
      onChange({ ...config, squareLength: inches * INCHES_TO_METERS });
    }
  };

  const handleMarkerLengthChange = (val: string) => {
    setMarkerLengthInch(val);
    const inches = parseFloat(val);
    if (!isNaN(inches)) {
      setPreset('Custom');
      onChange({ ...config, markerLength: inches * INCHES_TO_METERS });
    }
  };

  // Check if this is a paper preset (has page dimensions)
  const getPaperDimensions = (): { pageWidth: number; pageHeight: number } | null => {
    if (preset === 'A4') return { pageWidth: 210, pageHeight: 297 };
    if (preset === 'A3') return { pageWidth: 297, pageHeight: 420 };
    if (preset === 'Letter') return { pageWidth: 215.9, pageHeight: 279.4 };
    return null;
  };

  const paperDimensions = getPaperDimensions();
  const isPaperPreset = paperDimensions !== null;

  const handleDownload = () => {
    const link = document.createElement('a');

    if (isPaperPreset && paperDimensions) {
      // Use PDF endpoint for paper presets
      const params = new URLSearchParams({
        squaresX: config.squaresX.toString(),
        squaresY: config.squaresY.toString(),
        squareLength: config.squareLength.toString(),
        markerLength: config.markerLength.toString(),
        dictionary: config.dictionary,
        pageWidth: paperDimensions.pageWidth.toString(),
        pageHeight: paperDimensions.pageHeight.toString(),
      });
      link.href = `/api/calibration/board/pdf?${params.toString()}`;
      link.download = `charuco_board_${preset}.pdf`;
    } else {
      // Use PNG for screen preset or custom
      link.href = previewUrl;
      link.download = `charuco_board_${preset}.png`;
    }

    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  return (
    <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
      <Card>
        <CardHeader>
          <CardTitle>Board Configuration</CardTitle>
          <CardDescription>Configure the Charuco board for calibration.</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="space-y-2">
            <Label>Preset</Label>
            <Select value={preset} onValueChange={handlePresetChange}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="A4">A4 Paper</SelectItem>
                <SelectItem value="A3">A3 Paper</SelectItem>
                <SelectItem value="Letter">Letter Paper</SelectItem>
                <SelectItem value="Screen">Screen / Monitor</SelectItem>
                <SelectItem value="Custom">Custom</SelectItem>
              </SelectContent>
            </Select>
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <Label>Squares X</Label>
              <Input
                type="number"
                value={config.squaresX}
                onChange={(e) => {
                  setPreset('Custom');
                  onChange({ ...config, squaresX: parseInt(e.target.value) || 0 });
                }}
              />
            </div>
            <div className="space-y-2">
              <Label>Squares Y</Label>
              <Input
                type="number"
                value={config.squaresY}
                onChange={(e) => {
                  setPreset('Custom');
                  onChange({ ...config, squaresY: parseInt(e.target.value) || 0 });
                }}
              />
            </div>
          </div>

          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label>
                  {preset === 'Screen' ? 'Measured Square Size' : 'Square Length'} (in)
                </Label>
                <TooltipProvider>
                  <Tooltip>
                    <TooltipTrigger>
                      <HelpCircle className="w-4 h-4 text-muted-foreground" />
                    </TooltipTrigger>
                    <TooltipContent>
                      <p className="max-w-xs">
                        The physical side length of each black square on the printed board or screen.
                        {preset === 'Screen' && " IMPORTANT: Measure this directly on your screen with a ruler."}
                      </p>
                    </TooltipContent>
                  </Tooltip>
                </TooltipProvider>
              </div>
              <Input
                type="number"
                step="0.01"
                value={squareLengthInch}
                onChange={(e) => handleSquareLengthChange(e.target.value)}
              />
            </div>
            <div className="space-y-2">
              <div className="flex items-center gap-2">
                <Label>Marker Length (in)</Label>
                <TooltipProvider>
                  <Tooltip>
                    <TooltipTrigger>
                      <HelpCircle className="w-4 h-4 text-muted-foreground" />
                    </TooltipTrigger>
                    <TooltipContent>
                      <p className="max-w-xs">
                        The physical side length of the ArUco markers inside the white squares.
                        Usually about 70-80% of the square length.
                      </p>
                    </TooltipContent>
                  </Tooltip>
                </TooltipProvider>
              </div>
              <Input
                type="number"
                step="0.01"
                value={markerLengthInch}
                onChange={(e) => handleMarkerLengthChange(e.target.value)}
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Dictionary</Label>
            <Select value={config.dictionary} onValueChange={(v) => onChange({ ...config, dictionary: v })}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="DICT_4X4_50">DICT_4X4_50</SelectItem>
                <SelectItem value="DICT_5X5_50">DICT_5X5_50</SelectItem>
                <SelectItem value="DICT_6X6_50">DICT_6X6_50</SelectItem>
              </SelectContent>
            </Select>
          </div>

          <div className="pt-4 flex gap-2">
            <Button variant="outline" onClick={handleDownload} className="flex-1">
              <Download className="w-4 h-4 mr-2" />
              {isPaperPreset ? 'Download PDF' : 'Download PNG'}
            </Button>
            <Button onClick={onNext} disabled={nextDisabled} className="flex-1">
              Next Step
            </Button>
          </div>
        </CardContent>
      </Card>

      <Card className="flex items-center justify-center bg-neutral-100 dark:bg-neutral-900 overflow-hidden">
        <div className="p-4">
          {previewUrl && (
            <img src={previewUrl} alt="Charuco Board Preview" className="max-w-full h-auto shadow-lg border bg-white" />
          )}
        </div>
      </Card>
    </div>
  );
}
