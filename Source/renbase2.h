// based on CBaseVideoRenderer from DirectShow base classes

#include "FrameStats.h"

// CBaseVideoRenderer2 is a renderer class (see its ancestor class) and
// it handles scheduling of media samples so that they are drawn at the
// correct time by the reference clock.  It implements a degradation
// strategy.  Possible degradation modes are:
//    Drop frames here (only useful if the drawing takes significant time)
//    Signal supplier (upstream) to drop some frame(s) - i.e. one-off skip.
//    Signal supplier to change the frame rate - i.e. ongoing skipping.
//    Or any combination of the above.
// In order to determine what's useful to try we need to know what's going
// on.  This is done by timing various operations (including the supplier).
// This timing is done by using timeGetTime as it is accurate enough and
// usually cheaper than calling the reference clock.  It also tells the
// truth if there is an audio break and the reference clock stops.
// We provide a number of public entry points (named OnXxxStart, OnXxxEnd)
// which the rest of the renderer calls at significant moments.  These do
// the timing.

class CBaseVideoRenderer2 : public CBaseRenderer,    // Base renderer class
                           public IQualProp,        // Property page guff
                           public IQualityControl   // Allow throttling
{
protected:
    // Hungarian:
    //     tFoo is the time Foo in mSec (beware m_tStart from filter.h)
    //     trBar is the time Bar by the reference clock

    //******************************************************************
    // State variables to control synchronisation
    //******************************************************************

    // Control of sending Quality messages.  We need to know whether
    // we are in trouble (e.g. frames being dropped) and where the time
    // is being spent.

    // When we drop a frame we play the next one early.
    // The frame after that is likely to wait before drawing and counting this
    // wait as spare time is unfair, so we count it as a zero wait.
    // We therefore need to know whether we are playing frames early or not.

    int m_nNormal;                  // The number of consecutive frames
                                    // drawn at their normal time (not early)
                                    // -1 means we just dropped a frame.

    BOOL m_bSupplierHandlingQuality;// The response to Quality messages says
                                    // our supplier is handling things.
                                    // We will allow things to go extra late
                                    // before dropping frames.  We will play
                                    // very early after he has dropped one.

    // Control of scheduling, frame dropping etc.
    // We need to know where the time is being spent so as to tell whether
    // we should be taking action here, signalling supplier or what.
    // The variables are initialised to a mode of NOT dropping frames.
    // They will tell the truth after a few frames.
    // We typically record a start time for an event, later we get the time
    // again and subtract to get the elapsed time, and we average this over
    // a few frames.  The average is used to tell what mode we are in.

    // Although these are reference times (64 bit) they are all DIFFERENCES
    // between times which are small.  An int will go up to 214 secs before
    // overflow.  Avoiding 64 bit multiplications and divisions seems
    // worth while.



    // Audio-video throttling.  If the user has turned up audio quality
    // very high (in principle it could be any other stream, not just audio)
    // then we can receive cries for help via the graph manager.  In this case
    // we put in a wait for some time after rendering each frame.
    int m_trThrottle;

    // The time taken to render (i.e. BitBlt) frames controls which component
    // needs to degrade.  If the blt is expensive, the renderer degrades.
    // If the blt is cheap it's done anyway and the supplier degrades.
    int m_trRenderAvg;              // Time frames are taking to blt
    int m_trRenderLast;             // Time for last frame blt
    int m_tRenderStart;             // Just before we started drawing (mSec)
                                    // derived from timeGetTime.

    // When frames are dropped we will play the next frame as early as we can.
    // If it was a false alarm and the machine is fast we slide gently back to
    // normal timing.  To do this, we record the offset showing just how early
    // we really are.  This will normally be negative meaning early or zero.
    int m_trEarliness;

    // Target provides slow long-term feedback to try to reduce the
    // average sync offset to zero.  Whenever a frame is actually rendered
    // early we add a msec or two, whenever late we take off a few.
    // We add or take off 1/32 of the error time.
    // Eventually we should be hovering around zero.  For a really bad case
    // where we were (say) 300mSec off, it might take 100 odd frames to
    // settle down.  The rate of change of this is intended to be slower
    // than any other mechanism in Quartz, thereby avoiding hunting.
    int m_trTarget;

    // The proportion of time spent waiting for the right moment to blt
    // controls whether we bother to drop a frame or whether we reckon that
    // we're doing well enough that we can stand a one-frame glitch.
    int m_trWaitAvg;                // Average of last few wait times
                                    // (actually we just average how early
                                    // we were).  Negative here means LATE.

    // The average inter-frame time.
    // This is used to calculate the proportion of the time used by the
    // three operations (supplying us, waiting, rendering)
    int m_trFrameAvg;               // Average inter-frame time
    int m_trDuration;               // duration of last frame.

    REFERENCE_TIME m_trRememberStampForPerf;  // original time stamp of frame
                                              // with no earliness fudges etc.

    // PROPERTY PAGE
    // This has edit fields that show the user what's happening
    // These member variables hold these counts.

    int m_cFramesDropped;           // cumulative frames dropped IN THE RENDERER
    int m_cFramesDrawn;             // Frames since streaming started seen BY THE
                                    // RENDERER (some may be dropped upstream)

    // Next two support average sync offset and standard deviation of sync offset.
    LONGLONG m_iTotAcc;                  // Sum of accuracies in mSec
    LONGLONG m_iSumSqAcc;           // Sum of squares of (accuracies in mSec)

    // Next two allow jitter calculation.  Jitter is std deviation of frame time.
    REFERENCE_TIME m_trLastDraw;    // Time of prev frame (for inter-frame times)
    LONGLONG m_iSumSqFrameTime;     // Sum of squares of (inter-frame time in mSec)
    LONGLONG m_iSumFrameTime;            // Sum of inter-frame times in mSec

    // To get performance statistics on frame rate, jitter etc, we need
    // to record the lateness and inter-frame time.  What we actually need are the
    // data above (sum, sum of squares and number of entries for each) but the data
    // is generated just ahead of time and only later do we discover whether the
    // frame was actually drawn or not.  So we have to hang on to the data
    int m_trLate;                   // hold onto frame lateness
    int m_trFrame;                  // hold onto inter-frame time

    int m_tStreamingStart;          // if streaming then time streaming started
                                    // else time of last streaming session
                                    // used for property page statistics

	CFrameStats m_FrameStats; // Used to measure the frame rate of the input video

public:
    CBaseVideoRenderer2(REFCLSID RenderClass, // CLSID for this renderer
                       __in_opt LPCTSTR pName,         // Debug ONLY description
                       __inout_opt LPUNKNOWN pUnk,       // Aggregated owner object
                       __inout HRESULT *phr);        // General OLE return code

    ~CBaseVideoRenderer2();

    // IQualityControl methods - Notify allows audio-video throttling

    STDMETHODIMP SetSink( IQualityControl * piqc);
    STDMETHODIMP Notify( IBaseFilter * pSelf, Quality q);

    // These provide a full video quality management implementation

    void OnRenderStart(IMediaSample *pMediaSample);
    void OnRenderEnd(IMediaSample *pMediaSample);
    void OnWaitStart();
    void OnWaitEnd();
    HRESULT OnStartStreaming();
    HRESULT OnStopStreaming();
    void ThrottleWait();

    // Handle the statistics gathering for our quality management

    void PreparePerformanceData(int trLate, int trFrame);
    virtual void RecordFrameLateness(int trLate, int trFrame);
    virtual void OnDirectRender(IMediaSample *pMediaSample);
    virtual HRESULT ResetStreamingTimes();
    BOOL ScheduleSample(IMediaSample *pMediaSample);
    HRESULT ShouldDrawSampleNow(IMediaSample *pMediaSample,
                                __inout REFERENCE_TIME *ptrStart,
                                __inout REFERENCE_TIME *ptrEnd);

    virtual HRESULT SendQuality(REFERENCE_TIME trLate, REFERENCE_TIME trRealStream);
    STDMETHODIMP JoinFilterGraph(__inout_opt IFilterGraph * pGraph, __in_opt LPCWSTR pName);

    //
    //  Do estimates for standard deviations for per-frame
    //  statistics
    //
    //  *piResult = (llSumSq - iTot * iTot / m_cFramesDrawn - 1) /
    //                            (m_cFramesDrawn - 2)
    //  or 0 if m_cFramesDrawn <= 3
    //
    HRESULT GetStdDev(
        int nSamples,
        __out int *piResult,
        LONGLONG llSumSq,
        LONGLONG iTot
    );
public:

    // IQualProp property page support

    STDMETHODIMP get_FramesDroppedInRenderer(__out int *cFramesDropped);
    STDMETHODIMP get_FramesDrawn(__out int *pcFramesDrawn);
    STDMETHODIMP get_AvgFrameRate(__out int *piAvgFrameRate);
    STDMETHODIMP get_Jitter(__out int *piJitter);
    STDMETHODIMP get_AvgSyncOffset(__out int *piAvg);
    STDMETHODIMP get_DevSyncOffset(__out int *piDev);

    // Implement an IUnknown interface and expose IQualProp

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid,__deref_out VOID **ppv);
};
