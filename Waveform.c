#include "Waveform.h"

#include <math.h>
#include <stdlib.h>


// Generate 1D (undershoot + trace + retrace).
// The trace part spans voltage scanStart to scanEnd.
void
GenerateGalvoWaveform(int32_t effectiveScanLen, int32_t retraceLen,
	int32_t undershootLen, double scanStart, double scanEnd, double *waveform)
{
	double scanAmplitude = scanEnd - scanStart;
	double step = scanAmplitude / (effectiveScanLen - 1);
	int32_t linearLen = undershootLen + effectiveScanLen;

	// Generate the linear scan curve
	double undershootStart = scanStart - undershootLen * step;
	for (int i = 0; i < linearLen; ++i)
	{
		waveform[i] = undershootStart + scanAmplitude * ((double)i / (effectiveScanLen - 1));
	}

	// Generate the rescan curve
	// Slope at start end end are both equal to the linear scan
	if (retraceLen > 0)
	{
		SplineInterpolate(retraceLen, scanEnd, undershootStart, step, step, waveform + linearLen);
	}
}


// n = number of elements
// slope in units of per element
void SplineInterpolate(int32_t n, double yFirst, double yLast,
	double slopeFirst, double slopeLast, double *result)
{
	double c[4];

	c[0] = slopeFirst / (n*n) + 2.0 / (n*n*n)*yFirst + slopeLast / (n*n) - 2.0 / (n*n*n)*yLast;
	c[1] = 3.0 / (n*n)*yLast - slopeLast / n - 2.0 / n*slopeFirst - 3.0 / (n*n)*yFirst;
	c[2] = slopeFirst;
	c[3] = yFirst;

	for (int32_t x = 0; x < n; x++)
	{
		result[x] = c[0] * x*x*x + c[1] * x*x + c[2] * x + c[3];
	}
}


/* Line clock pattern for NI DAQ to output from one of its digital IOs */
OScDev_RichError *GenerateLineClock(const struct WaveformParams *parameters, uint8_t* lineClock)
{
	uint32_t lineDelay = parameters->undershoot;
	uint32_t width = parameters->width;
	uint32_t height = parameters->height;

	uint32_t x_length = lineDelay + width + X_RETRACE_LEN;
	for (uint32_t j = 0; j < height; j++)
		for (uint32_t i = 0; i < x_length; i++)
			lineClock[i + j*x_length] =
			((i >= lineDelay) && (i < lineDelay + width)) ? 1 : 0;

	return OScDev_RichError_OK;
}

// High voltage right after a line acquisition is done
// like a line clock of reversed polarity
// specially for B&H FLIM application
OScDev_RichError *GenerateFLIMLineClock(const struct WaveformParams* parameters, uint8_t* lineClockFLIM)
{
	uint32_t lineDelay = parameters->undershoot;
	uint32_t width = parameters->width;
	uint32_t height = parameters->height;

	uint32_t x_length = lineDelay + width + X_RETRACE_LEN;
	for (uint32_t j = 0; j < height; j++)
		for (uint32_t i = 0; i < x_length; i++)
			lineClockFLIM[i + j*x_length] = (i >= lineDelay + width) ? 1 : 0;

	return OScDev_RichError_OK;
}

// Frame clock for B&H FLIM
// High voltage at the end of the frame
OScDev_RichError *GenerateFLIMFrameClock(const struct WaveformParams* parameters, uint8_t* frameClockFLIM)
{
	uint32_t lineDelay = parameters->undershoot;
	uint32_t width = parameters->width;
	uint32_t height = parameters->height;

	uint32_t x_length = lineDelay + width + X_RETRACE_LEN;

	for (uint32_t j = 0; j < height; ++j)
		for (uint32_t i = 0; i < x_length; ++i)
			frameClockFLIM[i + j * x_length] =
			((j == height - 1) && (i > lineDelay + width)) ? 1 : 0;

	return OScDev_RichError_OK;
}

int32_t GetClockWaveformSize(const struct WaveformParams* parameters)
{
	uint32_t elementsPerLine = parameters->undershoot + parameters->width + X_RETRACE_LEN;
	uint32_t height = parameters->height;
	int32_t elementsPerFramePerChan = elementsPerLine * height;

	return elementsPerFramePerChan;
}

int32_t GetScannerWaveformSize(const struct WaveformParams* parameters)
{
	uint32_t elementsPerLine = parameters->undershoot + parameters->width + X_RETRACE_LEN;
	uint32_t height = parameters->height;
	uint32_t yLen = height + Y_RETRACE_LEN;
	int32_t totalElementsPerFramePerChan = elementsPerLine * yLen;   // including y retrace portion

	return totalElementsPerFramePerChan;
}


/*
Generate X and Y waveforms in analog format (voltage) for a whole frame scan
Format: X|Y in a 1D array for NI DAQ to simultaneously output in two channels
Analog voltage range (-0.5V, 0.5V) at zoom 1
Including Y retrace waveform that moves the slow galvo back to its starting position
*/
OScDev_RichError
*GenerateGalvoWaveformFrame(const struct WaveformParams* parameters, double *xyWaveformFrame)
{
	uint32_t pixelsPerLine = parameters->width; // ROI size
	uint32_t linesPerFrame = parameters->height;
	uint32_t resolution = parameters->resolution;
	uint32_t zoom = parameters->zoom;
	uint32_t undershoot = parameters->undershoot;
	uint32_t xOffset = parameters->xOffset; // ROI offset
	uint32_t yOffset = parameters->yOffset;
	uint32_t galvoOffsetX = parameters->galvoOffsetX; // Adjustment Offset
	uint32_t galvoOffsetY = parameters->galvoOffsetY;

	// Voltage ranges of the ROI
	double xStart = (-0.5 * resolution + xOffset) / (zoom * resolution);
	double yStart = (-0.5 * resolution + yOffset) / (zoom * resolution);
	double xEnd = xStart + pixelsPerLine / (zoom * resolution);
	double yEnd = yStart + linesPerFrame / (zoom * resolution);

	size_t xLength = undershoot + pixelsPerLine + X_RETRACE_LEN;
	size_t yLength = linesPerFrame + Y_RETRACE_LEN;
	double *xWaveform = (double *)malloc(sizeof(double) * xLength);
	double *yWaveform = (double *)malloc(sizeof(double) * yLength);
	GenerateGalvoWaveform(pixelsPerLine, X_RETRACE_LEN, undershoot, xStart, xEnd, xWaveform);
	GenerateGalvoWaveform(linesPerFrame, Y_RETRACE_LEN, 0, yStart, yEnd, yWaveform);

	// convert to optical degree assuming 10V equal to 30 optical degree
	// TODO We shouldn't make such an assumption! Also I think the variable
	// names are the other way around ("inDegree" means "in volts" here).
	double offsetXinDegree = galvoOffsetX / 3.0;
	double offsetYinDegree = galvoOffsetY / 3.0;

	// effective scan waveform for a whole frame
	for (unsigned j = 0; j < yLength; ++j)
	{
		for (unsigned i = 0; i < xLength; ++i)
		{
			// first half is X waveform,
			// x line scan repeated yLength times (sawteeth) 
			// galvo x stays at starting position after one frame is scanned
			xyWaveformFrame[i + j*xLength] = (j < linesPerFrame) ?
				(xWaveform[i] + offsetXinDegree) : (xWaveform[0] + offsetXinDegree);
			//xyWaveformFrame[i + j*xLength] = xWaveform[i];
			// second half is Y waveform
			// at each x (fast) scan line, y value is constant
			// effectively y retrace takes (Y_RETRACE_LENGTH * xLength) steps
			xyWaveformFrame[i + j*xLength + yLength*xLength] = (yWaveform[j] + offsetYinDegree);
		}
	}
	// TODO When we are scanning multiple frames, the Y retrace can be
	// simultaneous with the last line's X retrace. (Spline interpolate
	// with zero slope at each end of retrace.)
	// TODO Simpler to use interleaved x,y format?

	free(xWaveform);
	free(yWaveform);

	return OScDev_RichError_OK;
}