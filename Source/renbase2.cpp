// based on CBaseVideoRenderer from DirectShow base classes

#include "stdafx.h"
#include "renbase2.h"

//  Helper function for clamping time differences
int inline TimeDiff(REFERENCE_TIME rt)
{
    if (rt < - (50 * UNITS)) {
        return -(50 * UNITS);
    } else
    if (rt > 50 * UNITS) {
        return 50 * UNITS;
    } else return (int)rt;
}

CBaseVideoRenderer2::CBaseVideoRenderer2(
      REFCLSID RenderClass, // CLSID for this renderer
      __in_opt LPCTSTR pName,         // Debug ONLY description
      __inout_opt LPUNKNOWN pUnk,       // Aggregated owner object
      __inout HRESULT *phr) :       // General OLE return code

    CBaseRenderer(RenderClass,pName,pUnk,phr),
    m_cFramesDropped(0),
    m_cFramesDrawn(0),
    m_bSupplierHandlingQuality(FALSE)
{
    ResetStreamingTimes();
} // Constructor


// Destructor is just a placeholder

CBaseVideoRenderer2::~CBaseVideoRenderer2()
{
    ASSERT(m_dwAdvise == 0);
}


// The timing functions in this class are called by the window object and by
// the renderer's allocator.
// The windows object calls timing functions as it receives media sample
// images for drawing using GDI.
// The allocator calls timing functions when it starts passing DCI/DirectDraw
// surfaces which are not rendered in the same way; The decompressor writes
// directly to the surface with no separate rendering, so those code paths
// call direct into us.  Since we only ever hand out DCI/DirectDraw surfaces
// when we have allocated one and only one image we know there cannot be any
// conflict between the two.
//
// We use timeGetTime to return the timing counts we use (since it's relative
// performance we are interested in rather than absolute compared to a clock)
// The window object sets the accuracy of the system clock (normally 1ms) by
// calling timeBeginPeriod/timeEndPeriod when it changes streaming states


// Reset all times controlling streaming.
// Set them so that
// 1. Frames will not initially be dropped
// 2. The first frame will definitely be drawn (achieved by saying that there
//    has not ben a frame drawn for a long time).

HRESULT CBaseVideoRenderer2::ResetStreamingTimes()
{
    m_trLastDraw = -1000;     // set up as first frame since ages (1 sec) ago
    m_tStreamingStart = timeGetTime();
    m_trRenderAvg = 0;
    m_trFrameAvg = -1;        // -1000 fps == "unset"
    m_trDuration = 0;         // 0 - strange value
    m_trRenderLast = 0;
    m_trWaitAvg = 0;
    m_tRenderStart = 0;
    m_cFramesDrawn = 0;
    m_cFramesDropped = 0;
    m_iTotAcc = 0;
    m_iSumSqAcc = 0;
    m_iSumSqFrameTime = 0;
    m_trFrame = 0;          // hygeine - not really needed
    m_trLate = 0;           // hygeine - not really needed
    m_iSumFrameTime = 0;
    m_nNormal = 0;
    m_trEarliness = 0;
    m_trTarget = -300000;  // 30mSec early
    m_trThrottle = 0;
    m_trRememberStampForPerf = 0;

	m_DrawStats.Reset();

    return NOERROR;
} // ResetStreamingTimes


// Reset all times controlling streaming. Note that we're now streaming. We
// don't need to set the rendering event to have the source filter released
// as it is done during the Run processing. When we are run we immediately
// release the source filter thread and draw any image waiting (that image
// may already have been drawn once as a poster frame while we were paused)

HRESULT CBaseVideoRenderer2::OnStartStreaming()
{
    ResetStreamingTimes();
    return NOERROR;
} // OnStartStreaming


// Called at end of streaming.  Fixes times for property page report

HRESULT CBaseVideoRenderer2::OnStopStreaming()
{
    m_tStreamingStart = timeGetTime()-m_tStreamingStart;
    return NOERROR;
} // OnStopStreaming


// Called when we start waiting for a rendering event.
// Used to update times spent waiting and not waiting.

void CBaseVideoRenderer2::OnWaitStart()
{
} // OnWaitStart


// Called when we are awoken from the wait in the window OR by our allocator
// when it is hanging around until the next sample is due for rendering on a
// DCI/DirectDraw surface. We add the wait time into our rolling average.
// We grab the interface lock so that we're serialised with the application
// thread going through the run code - which in due course ends up calling
// ResetStreaming times - possibly as we run through this section of code

void CBaseVideoRenderer2::OnWaitEnd()
{
} // OnWaitEnd


// Put data on one side that describes the lateness of the current frame.
// We don't yet know whether it will actually be drawn.  In direct draw mode,
// this decision is up to the filter upstream, and it could change its mind.
// The rules say that if it did draw it must call Receive().  One way or
// another we eventually get into either OnRenderStart or OnDirectRender and
// these both call RecordFrameLateness to update the statistics.

void CBaseVideoRenderer2::PreparePerformanceData(int trLate, int trFrame)
{
    m_trLate = trLate;
    m_trFrame = trFrame;
} // PreparePerformanceData


// update the statistics:
// m_iTotAcc, m_iSumSqAcc, m_iSumSqFrameTime, m_iSumFrameTime, m_cFramesDrawn
// Note that because the properties page reports using these variables,
// 1. We need to be inside a critical section
// 2. They must all be updated together.  Updating the sums here and the count
// elsewhere can result in imaginary jitter (i.e. attempts to find square roots
// of negative numbers) in the property page code.

void CBaseVideoRenderer2::RecordFrameLateness(int trLate, int trFrame)
{
    // Record how timely we are.
    int tLate = trLate/10000;

    // Best estimate of moment of appearing on the screen is average of
    // start and end draw times.  Here we have only the end time.  This may
    // tend to show us as spuriously late by up to 1/2 frame rate achieved.
    // Decoder probably monitors draw time.  We don't bother.

    // This is a kludge - we can get frames that are very late
    // especially (at start-up) and they invalidate the statistics.
    // So ignore things that are more than 1 sec off.
    if (tLate>1000 || tLate<-1000) {
        if (m_cFramesDrawn<=1) {
            tLate = 0;
        } else if (tLate>0) {
            tLate = 1000;
        } else {
            tLate = -1000;
        }
    }
    // The very first frame often has a invalid time, so don't
    // count it into the statistics.   (???)
    if (m_cFramesDrawn>1) {
        m_iTotAcc += tLate;
        m_iSumSqAcc += (tLate*tLate);
    }

    // calculate inter-frame time.  Doesn't make sense for first frame
    // second frame suffers from invalid first frame stamp.
    if (m_cFramesDrawn>2) {
        int tFrame = trFrame/10000;    // convert to mSec else it overflows

        // This is a kludge.  It can overflow anyway (a pause can cause
        // a very long inter-frame time) and it overflows at 2**31/10**7
        // or about 215 seconds i.e. 3min 35sec
        if (tFrame>1000||tFrame<0) tFrame = 1000;
        m_iSumSqFrameTime += tFrame*tFrame;
        ASSERT(m_iSumSqFrameTime>=0);
        m_iSumFrameTime += tFrame;
    }
    ++m_cFramesDrawn;

} // RecordFrameLateness


void CBaseVideoRenderer2::ThrottleWait()
{
    if (m_trThrottle>0) {
        int iThrottle = m_trThrottle/10000;    // convert to mSec
        DbgLog((LOG_TRACE, 0, TEXT("Throttle %d ms"), iThrottle));
        Sleep(iThrottle);
    } else {
        Sleep(0);
    }
} // ThrottleWait


// Whenever a frame is rendered it goes though either OnRenderStart
// or OnDirectRender.  Data that are generated during ShouldDrawSample
// are added to the statistics by calling RecordFrameLateness from both
// these two places.

// Called in place of OnRenderStart..OnRenderEnd
// When a DirectDraw image is drawn
void CBaseVideoRenderer2::OnDirectRender(IMediaSample *pMediaSample)
{
    m_trRenderAvg = 0;
    m_trRenderLast = 5000000;  // If we mode switch, we do NOT want this
                               // to inhibit the new average getting going!
                               // so we set it to half a second
    RecordFrameLateness(m_trLate, m_trFrame);
    ThrottleWait();
} // OnDirectRender


// Called just before we start drawing.  All we do is to get the current clock
// time (from the system) and return.  We have to store the start render time
// in a member variable because it isn't used until we complete the drawing
// The rest is just performance logging.

void CBaseVideoRenderer2::OnRenderStart(IMediaSample *pMediaSample)
{
    RecordFrameLateness(m_trLate, m_trFrame);
    m_tRenderStart = timeGetTime();
} // OnRenderStart


// Called directly after drawing an image.  We calculate the time spent in the
// drawing code and if this doesn't appear to have any odd looking spikes in
// it then we add it to the current average draw time.  Measurement spikes may
// occur if the drawing thread is interrupted and switched to somewhere else.

void CBaseVideoRenderer2::OnRenderEnd(IMediaSample *pMediaSample)
{
    // The renderer time can vary erratically if we are interrupted so we do
    // some smoothing to help get more sensible figures out but even that is
    // not enough as figures can go 9,10,9,9,83,9 and we must disregard 83

    int tr = (timeGetTime() - m_tRenderStart)*10000;   // convert mSec->UNITS
    if (tr < m_trRenderAvg*2 || tr < 2 * m_trRenderLast) {
        // DO_MOVING_AVG(m_trRenderAvg, tr);
        m_trRenderAvg = (tr + (AVGPERIOD-1)*m_trRenderAvg)/AVGPERIOD;
    }
    m_trRenderLast = tr;
    ThrottleWait();
} // OnRenderEnd


STDMETHODIMP CBaseVideoRenderer2::SetSink( IQualityControl * piqc)
{

    m_pQSink = piqc;

    return NOERROR;
} // SetSink


STDMETHODIMP CBaseVideoRenderer2::Notify( IBaseFilter * pSelf, Quality q)
{
    // NOTE:  We are NOT getting any locks here.  We could be called
    // asynchronously and possibly even on a time critical thread of
    // someone else's - so we do the minumum.  We only set one state
    // variable (an integer) and if that happens to be in the middle
    // of another thread reading it they will just get either the new
    // or the old value.  Locking would achieve no more than this.

    // It might be nice to check that we are being called from m_pGraph, but
    // it turns out to be a millisecond or so per throw!

    // This is heuristics, these numbers are aimed at being "what works"
    // rather than anything based on some theory.
    // We use a hyperbola because it's easy to calculate and it includes
    // a panic button asymptote (which we push off just to the left)
    // The throttling fits the following table (roughly)
    // Proportion   Throttle (msec)
    //     >=1000         0
    //        900         3
    //        800         7
    //        700        11
    //        600        17
    //        500        25
    //        400        35
    //        300        50
    //        200        72
    //        125       100
    //        100       112
    //         50       146
    //          0       200

    // (some evidence that we could go for a sharper kink - e.g. no throttling
    // until below the 750 mark - might give fractionally more frames on a
    // P60-ish machine).  The easy way to get these coefficients is to use
    // Renbase.xls follow the instructions therein using excel solver.

    if (q.Proportion>=1000) { m_trThrottle = 0; }
    else {
        // The DWORD is to make quite sure I get unsigned arithmetic
        // as the constant is between 2**31 and 2**32
        m_trThrottle = -330000 + (388880000/(q.Proportion+167));
    }
    return NOERROR;
} // Notify


// Send a message to indicate what our supplier should do about quality.
// Theory:
// What a supplier wants to know is "is the frame I'm working on NOW
// going to be late?".
// F1 is the frame at the supplier (as above)
// Tf1 is the due time for F1
// T1 is the time at that point (NOW!)
// Tr1 is the time that f1 WILL actually be rendered
// L1 is the latency of the graph for frame F1 = Tr1-T1
// D1 (for delay) is how late F1 will be beyond its due time i.e.
// D1 = (Tr1-Tf1) which is what the supplier really wants to know.
// Unfortunately Tr1 is in the future and is unknown, so is L1
//
// We could estimate L1 by its value for a previous frame,
// L0 = Tr0-T0 and work off
// D1' = ((T1+L0)-Tf1) = (T1 + (Tr0-T0) -Tf1)
// Rearranging terms:
// D1' = (T1-T0) + (Tr0-Tf1)
//       adding (Tf0-Tf0) and rearranging again:
//     = (T1-T0) + (Tr0-Tf0) + (Tf0-Tf1)
//     = (T1-T0) - (Tf1-Tf0) + (Tr0-Tf0)
// But (Tr0-Tf0) is just D0 - how late frame zero was, and this is the
// Late field in the quality message that we send.
// The other two terms just state what correction should be applied before
// using the lateness of F0 to predict the lateness of F1.
// (T1-T0) says how much time has actually passed (we have lost this much)
// (Tf1-Tf0) says how much time should have passed if we were keeping pace
// (we have gained this much).
//
// Suppliers should therefore work off:
//    Quality.Late + (T1-T0)  - (Tf1-Tf0)
// and see if this is "acceptably late" or even early (i.e. negative).
// They get T1 and T0 by polling the clock, they get Tf1 and Tf0 from
// the time stamps in the frames.  They get Quality.Late from us.
//

HRESULT CBaseVideoRenderer2::SendQuality(REFERENCE_TIME trLate,
                                        REFERENCE_TIME trRealStream)
{
    Quality q;
    HRESULT hr;

    // If we are the main user of time, then report this as Flood/Dry.
    // If our suppliers are, then report it as Famine/Glut.
    //
    // We need to take action, but avoid hunting.  Hunting is caused by
    // 1. Taking too much action too soon and overshooting
    // 2. Taking too long to react (so averaging can CAUSE hunting).
    //
    // The reason why we use trLate as well as Wait is to reduce hunting;
    // if the wait time is coming down and about to go into the red, we do
    // NOT want to rely on some average which is only telling is that it used
    // to be OK once.

    q.TimeStamp = (REFERENCE_TIME)trRealStream;

    if (m_trFrameAvg<0) {
        q.Type = Famine;      // guess
    }
    // Is the greater part of the time taken bltting or something else
    else if (m_trFrameAvg > 2*m_trRenderAvg) {
        q.Type = Famine;                        // mainly other
    } else {
        q.Type = Flood;                         // mainly bltting
    }

    q.Proportion = 1000;               // default

    if (m_trFrameAvg<0) {
        // leave it alone - we don't know enough
    }
    else if ( trLate> 0 ) {
        // try to catch up over the next second
        // We could be Really, REALLY late, but rendering all the frames
        // anyway, just because it's so cheap.

        q.Proportion = 1000 - (int)((trLate)/(UNITS/1000));
        if (q.Proportion<500) {
           q.Proportion = 500;      // don't go daft. (could've been negative!)
        } else {
        }

    } else if (  m_trWaitAvg>20000
              && trLate<-20000
              ){
        // Go cautiously faster - aim at 2mSec wait.
        if (m_trWaitAvg>=m_trFrameAvg) {
            // This can happen because of some fudges.
            // The waitAvg is how long we originally planned to wait
            // The frameAvg is more honest.
            // It means that we are spending a LOT of time waiting
            q.Proportion = 2000;    // double.
        } else {
            if (m_trFrameAvg+20000 > m_trWaitAvg) {
                q.Proportion
                    = 1000 * (m_trFrameAvg / (m_trFrameAvg + 20000 - m_trWaitAvg));
            } else {
                // We're apparently spending more than the whole frame time waiting.
                // Assume that the averages are slightly out of kilter, but that we
                // are indeed doing a lot of waiting.  (This leg probably never
                // happens, but the code avoids any potential divide by zero).
                q.Proportion = 2000;
            }
        }

        if (q.Proportion>2000) {
            q.Proportion = 2000;    // don't go crazy.
        }
    }

    // Tell the supplier how late frames are when they get rendered
    // That's how late we are now.
    // If we are in directdraw mode then the guy upstream can see the drawing
    // times and we'll just report on the start time.  He can figure out any
    // offset to apply.  If we are in DIB Section mode then we will apply an
    // extra offset which is half of our drawing time.  This is usually small
    // but can sometimes be the dominant effect.  For this we will use the
    // average drawing time rather than the last frame.  If the last frame took
    // a long time to draw and made us late, that's already in the lateness
    // figure.  We should not add it in again unless we expect the next frame
    // to be the same.  We don't, we expect the average to be a better shot.
    // In direct draw mode the RenderAvg will be zero.

    q.Late = trLate + m_trRenderAvg/2;

    // A specific sink interface may be set through IPin

    if (m_pQSink==NULL) {
        // Get our input pin's peer.  We send quality management messages
        // to any nominated receiver of these things (set in the IPin
        // interface), or else to our source filter.

        IQualityControl *pQC = NULL;
        IPin *pOutputPin = m_pInputPin->GetConnected();
        ASSERT(pOutputPin != NULL);

        // And get an AddRef'd quality control interface

        hr = pOutputPin->QueryInterface(IID_IQualityControl,(void**) &pQC);
        if (SUCCEEDED(hr)) {
            m_pQSink = pQC;
        }
    }
    if (m_pQSink) {
        return m_pQSink->Notify(this,q);
    }

    return S_FALSE;

} // SendQuality


// We are called with a valid IMediaSample image to decide whether this is to
// be drawn or not.  There must be a reference clock in operation.
// Return S_OK if it is to be drawn Now (as soon as possible)
// Return S_FALSE if it is to be drawn when it's due
// Return an error if we want to drop it
// m_nNormal=-1 indicates that we dropped the previous frame and so this
// one should be drawn early.  Respect it and update it.
// Use current stream time plus a number of heuristics (detailed below)
// to make the decision

HRESULT CBaseVideoRenderer2::ShouldDrawSampleNow(IMediaSample *pMediaSample,
                                                __inout REFERENCE_TIME *ptrStart,
                                                __inout REFERENCE_TIME *ptrEnd)
{

    // Don't call us unless there's a clock interface to synchronise with
    ASSERT(m_pClock);

    // We lose a bit of time depending on the monitor type waiting for the next
    // screen refresh.  On average this might be about 8mSec - so it will be
    // later than we think when the picture appears.  To compensate a bit
    // we bias the media samples by -8mSec i.e. 80000 UNITs.
    // We don't ever make a stream time negative (call it paranoia)
    if (*ptrStart>=80000) {
        *ptrStart -= 80000;
        *ptrEnd -= 80000;       // bias stop to to retain valid frame duration
    }

    // Cache the time stamp now.  We will want to compare what we did with what
    // we started with (after making the monitor allowance).
    m_trRememberStampForPerf = *ptrStart;

    // Get reference times (current and late)
    REFERENCE_TIME trRealStream;     // the real time now expressed as stream time.
    m_pClock->GetTime(&trRealStream);

    trRealStream -= m_tStart;     // convert to stream time (this is a reftime)

    // We have to wory about two versions of "lateness".  The truth, which we
    // try to work out here and the one measured against m_trTarget which
    // includes long term feedback.  We report statistics against the truth
    // but for operational decisions we work to the target.
    // We use TimeDiff to make sure we get an integer because we
    // may actually be late (or more likely early if there is a big time
    // gap) by a very long time.
    const int trTrueLate = TimeDiff(trRealStream - *ptrStart);
    const int trLate = trTrueLate;

    // Send quality control messages upstream, measured against target
    HRESULT hr = SendQuality(trLate, trRealStream);
    // Note: the filter upstream is allowed to this FAIL meaning "you do it".
    m_bSupplierHandlingQuality = (hr==S_OK);

    // Decision time!  Do we drop, draw when ready or draw immediately?

    const int trDuration = (int)(*ptrEnd - *ptrStart);
    {
        // We need to see if the frame rate of the file has just changed.
        // This would make comparing our previous frame rate with the current
        // frame rate inefficent.  Hang on a moment though.  I've seen files
        // where the frames vary between 33 and 34 mSec so as to average
        // 30fps.  A minor variation like that won't hurt us.
        int t = m_trDuration/32;
        if (  trDuration > m_trDuration+t
           || trDuration < m_trDuration-t
           ) {
            // There's a major variation.  Reset the average frame rate to
            // exactly the current rate to disable decision 9002 for this frame,
            // and remember the new rate.
            m_trFrameAvg = trDuration;
            m_trDuration = trDuration;
        }
    }

    // Control the graceful slide back from slow to fast machine mode.
    // After a frame drop accept an early frame and set the earliness to here
    // If this frame is already later than the earliness then slide it to here
    // otherwise do the standard slide (reduce by about 12% per frame).
    // Note: earliness is normally NEGATIVE
    BOOL bJustDroppedFrame
        = (  m_bSupplierHandlingQuality
          //  Can't use the pin sample properties because we might
          //  not be in Receive when we call this
          && (S_OK == pMediaSample->IsDiscontinuity())          // he just dropped one
          )
       || (m_nNormal==-1);                          // we just dropped one


    // Set m_trEarliness (slide back from slow to fast machine mode)
    if (trLate>0) {
        m_trEarliness = 0;   // we are no longer in fast machine mode at all!
    } else if (  (trLate>=m_trEarliness) || bJustDroppedFrame) {
        m_trEarliness = trLate;  // Things have slipped of their own accord
    } else {
        m_trEarliness = m_trEarliness - m_trEarliness/8;  // graceful slide
    }

    // prepare the new wait average - but don't pollute the old one until
    // we have finished with it.
    int trWaitAvg;
    {
        // We never mix in a negative wait.  This causes us to believe in fast machines
        // slightly more.
        int trL = trLate<0 ? -trLate : 0;
        trWaitAvg = (trL + m_trWaitAvg*(AVGPERIOD-1))/AVGPERIOD;
    }


    int trFrame;
    {
        REFERENCE_TIME tr = trRealStream - m_trLastDraw; // Cd be large - 4 min pause!
        if (tr>10000000) {
            tr = 10000000;   // 1 second - arbitrarily.
        }
        trFrame = int(tr);
    }

    // We will DRAW this frame IF...
    if (
          // ...the time we are spending drawing is a small fraction of the total
          // observed inter-frame time so that dropping it won't help much.
          (3*m_trRenderAvg <= m_trFrameAvg)

         // ...or our supplier is NOT handling things and the next frame would
         // be less timely than this one or our supplier CLAIMS to be handling
         // things, and is now less than a full FOUR frames late.
       || ( m_bSupplierHandlingQuality
          ? (trLate <= trDuration*4)
          : (trLate+trLate < trDuration)
          )

          // ...or we are on average waiting for over eight milliseconds then
          // this may be just a glitch.  Draw it and we'll hope to catch up.
       || (m_trWaitAvg > 80000)

          // ...or we haven't drawn an image for over a second.  We will update
          // the display, which stops the video looking hung.
          // Do this regardless of how late this media sample is.
       || ((trRealStream - m_trLastDraw) > UNITS)

    ) {
        HRESULT Result;

        // We are going to play this frame.  We may want to play it early.
        // We will play it early if we think we are in slow machine mode.
        // If we think we are NOT in slow machine mode, we will still play
        // it early by m_trEarliness as this controls the graceful slide back.
        // and in addition we aim at being m_trTarget late rather than "on time".

        BOOL bPlayASAP = FALSE;

        // we will play it AT ONCE (slow machine mode) if...

            // ...we are playing catch-up
        if ( bJustDroppedFrame) {
            bPlayASAP = TRUE;
        }

            // ...or if we are running below the true frame rate
            // exact comparisons are glitchy, for these measurements,
            // so add an extra 5% or so
        else if (  (m_trFrameAvg > trDuration + trDuration/16)

                   // It's possible to get into a state where we are losing ground, but
                   // are a very long way ahead.  To avoid this or recover from it
                   // we refuse to play early by more than 10 frames.
                && (trLate > - trDuration*10)
                ){
            bPlayASAP = TRUE;
        }
#if 0
            // ...or if we have been late and are less than one frame early
        else if (  (trLate + trDuration > 0)
                && (m_trWaitAvg<=20000)
                ) {
            bPlayASAP = TRUE;
        }
#endif
        // We will NOT play it at once if we are grossly early.  On very slow frame
        // rate movies - e.g. clock.avi - it is not a good idea to leap ahead just
        // because we got starved (for instance by the net) and dropped one frame
        // some time or other.  If we are more than 900mSec early, then wait.
        if (trLate<-9000000) {
            bPlayASAP = FALSE;
        }

        if (bPlayASAP) {

            m_nNormal = 0;
            // When we are here, we are in slow-machine mode.  trLate may well
            // oscillate between negative and positive when the supplier is
            // dropping frames to keep sync.  We should not let that mislead
            // us into thinking that we have as much as zero spare time!
            // We just update with a zero wait.
            m_trWaitAvg = (m_trWaitAvg*(AVGPERIOD-1))/AVGPERIOD;

            // Assume that we draw it immediately.  Update inter-frame stats
            m_trFrameAvg = (trFrame + m_trFrameAvg*(AVGPERIOD-1))/AVGPERIOD;

            // If this is NOT a perf build, then report what we know so far
            // without looking at the clock any more.  This assumes that we
            // actually wait for exactly the time we hope to.  It also reports
            // how close we get to the manipulated time stamps that we now have
            // rather than the ones we originally started with.  It will
            // therefore be a little optimistic.  However it's fast.
            PreparePerformanceData(trTrueLate, trFrame);

            m_trLastDraw = trRealStream;
            if (m_trEarliness > trLate) {
                m_trEarliness = trLate;  // if we are actually early, this is neg
            }
            Result = S_OK;                   // Draw it now

        } else {
            ++m_nNormal;
            // Set the average frame rate to EXACTLY the ideal rate.
            // If we are exiting slow-machine mode then we will have caught up
            // and be running ahead, so as we slide back to exact timing we will
            // have a longer than usual gap at this point.  If we record this
            // real gap then we'll think that we're running slow and go back
            // into slow-machine mode and vever get it straight.
            m_trFrameAvg = trDuration;

            // Play it early by m_trEarliness and by m_trTarget

            {
                int trE = m_trEarliness;
                if (trE < -m_trFrameAvg) {
                    trE = -m_trFrameAvg;
                }
                *ptrStart += trE;           // N.B. earliness is negative
            }

            int Delay = -trTrueLate;
            Result = Delay<=0 ? S_OK : S_FALSE;     // OK = draw now, FALSE = wait

            m_trWaitAvg = trWaitAvg;

            // Predict when it will actually be drawn and update frame stats

            if (Result==S_FALSE) {   // We are going to wait
                trFrame = TimeDiff(*ptrStart-m_trLastDraw);
                m_trLastDraw = *ptrStart;
            } else {
                // trFrame is already = trRealStream-m_trLastDraw;
                m_trLastDraw = trRealStream;
            }

            int iAccuracy;
            if (Delay>0) {
                // Report lateness based on when we intend to play it
                iAccuracy = TimeDiff(*ptrStart-m_trRememberStampForPerf);
            } else {
                // Report lateness based on playing it *now*.
                iAccuracy = trTrueLate;     // trRealStream-RememberStampForPerf;
            }
            PreparePerformanceData(iAccuracy, trFrame);
        }
        return Result;
    }

    // We are going to drop this frame!
    // Of course in DirectDraw mode the guy upstream may draw it anyway.

    // This will probably give a large negative wack to the wait avg.
    m_trWaitAvg = trWaitAvg;

    // We are going to drop this frame so draw the next one early
    // n.b. if the supplier is doing direct draw then he may draw it anyway
    // but he's doing something funny to arrive here in that case.

    m_nNormal = -1;
    return E_FAIL;                         // drop it

} // ShouldDrawSampleNow


// NOTE we're called by both the window thread and the source filter thread
// so we have to be protected by a critical section (locked before called)
// Also, when the window thread gets signalled to render an image, it always
// does so regardless of how late it is. All the degradation is done when we
// are scheduling the next sample to be drawn. Hence when we start an advise
// link to draw a sample, that sample's time will always become the last one
// drawn - unless of course we stop streaming in which case we cancel links

BOOL CBaseVideoRenderer2::ScheduleSample(IMediaSample *pMediaSample)
{
	REFERENCE_TIME StartTime, EndTime;
	if (pMediaSample && S_OK == pMediaSample->GetTime(&StartTime, &EndTime)) {
		m_FrameStats.Add(StartTime);
	}

    // We override ShouldDrawSampleNow to add quality management

    BOOL bDrawImage = CBaseRenderer::ScheduleSample(pMediaSample);
    if (bDrawImage == FALSE) {
	++m_cFramesDropped;
	return FALSE;
    }

    // m_cFramesDrawn must NOT be updated here.  It has to be updated
    // in RecordFrameLateness at the same time as the other statistics.
    return TRUE;
}


// Implementation of IQualProp interface needed to support the property page
// This is how the property page gets the data out of the scheduler. We are
// passed into the constructor the owning object in the COM sense, this will
// either be the video renderer or an external IUnknown if we're aggregated.
// We initialise our CUnknown base class with this interface pointer. Then
// all we have to do is to override NonDelegatingQueryInterface to expose
// our IQualProp interface. The AddRef and Release are handled automatically
// by the base class and will be passed on to the appropriate outer object

STDMETHODIMP CBaseVideoRenderer2::get_FramesDroppedInRenderer(__out int *pcFramesDropped)
{
    CheckPointer(pcFramesDropped,E_POINTER);
    CAutoLock cVideoLock(&m_InterfaceLock);
    *pcFramesDropped = m_cFramesDropped;
    return NOERROR;
} // get_FramesDroppedInRenderer


// Set *pcFramesDrawn to the number of frames drawn since
// streaming started.

STDMETHODIMP CBaseVideoRenderer2::get_FramesDrawn( int *pcFramesDrawn)
{
    CheckPointer(pcFramesDrawn,E_POINTER);
    CAutoLock cVideoLock(&m_InterfaceLock);
    *pcFramesDrawn = m_cFramesDrawn;
    return NOERROR;
} // get_FramesDrawn


// Set iAvgFrameRate to the frames per hundred secs since
// streaming started.  0 otherwise.

STDMETHODIMP CBaseVideoRenderer2::get_AvgFrameRate( int *piAvgFrameRate)
{
    CheckPointer(piAvgFrameRate,E_POINTER);
    CAutoLock cVideoLock(&m_InterfaceLock);

    int t;
    if (m_bStreaming) {
        t = timeGetTime()-m_tStreamingStart;
    } else {
        t = m_tStreamingStart;
    }

    if (t<=0) {
        *piAvgFrameRate = 0;
        ASSERT(m_cFramesDrawn == 0);
    } else {
        // i is frames per hundred seconds
        *piAvgFrameRate = MulDiv(100000, m_cFramesDrawn, t);
    }
    return NOERROR;
} // get_AvgFrameRate


// Set *piAvg to the average sync offset since streaming started
// in mSec.  The sync offset is the time in mSec between when the frame
// should have been drawn and when the frame was actually drawn.

STDMETHODIMP CBaseVideoRenderer2::get_AvgSyncOffset(__out int *piAvg)
{
    CheckPointer(piAvg,E_POINTER);
    CAutoLock cVideoLock(&m_InterfaceLock);

    if (NULL==m_pClock) {
        *piAvg = 0;
        return NOERROR;
    }

    // Note that we didn't gather the stats on the first frame
    // so we use m_cFramesDrawn-1 here
    if (m_cFramesDrawn<=1) {
        *piAvg = 0;
    } else {
        *piAvg = (int)(m_iTotAcc / (m_cFramesDrawn-1));
    }
    return NOERROR;
} // get_AvgSyncOffset


// To avoid dragging in the maths library - a cheap
// approximate integer square root.
// We do this by getting a starting guess which is between 1
// and 2 times too large, followed by THREE iterations of
// Newton Raphson.  (That will give accuracy to the nearest mSec
// for the range in question - roughly 0..1000)
//
// It would be faster to use a linear interpolation and ONE NR, but
// who cares.  If anyone does - the best linear interpolation is
// to approximates sqrt(x) by
// y = x * (sqrt(2)-1) + 1 - 1/sqrt(2) + 1/(8*(sqrt(2)-1))
// 0r y = x*0.41421 + 0.59467
// This minimises the maximal error in the range in question.
// (error is about +0.008883 and then one NR will give error .0000something
// (Of course these are integers, so you can't just multiply by 0.41421
// you'd have to do some sort of MulDiv).
// Anyone wanna check my maths?  (This is only for a property display!)

int isqrt_(int x)
{
    int s = 1;
    // Make s an initial guess for sqrt(x)
    if (x > 0x40000000) {
       s = 0x8000;     // prevent any conceivable closed loop
    } else {
        while (s*s<x) {    // loop cannot possible go more than 31 times
            s = 2*s;       // normally it goes about 6 times
        }
        // Three NR iterations.
        if (x==0) {
           s= 0; // Wouldn't it be tragic to divide by zero whenever our
                 // accuracy was perfect!
        } else {
            s = (s*s+x)/(2*s);
            if (s>=0) s = (s*s+x)/(2*s);
            if (s>=0) s = (s*s+x)/(2*s);
        }
    }
    return s;
}

//
//  Do estimates for standard deviations for per-frame
//  statistics
//
HRESULT CBaseVideoRenderer2::GetStdDev(
    int nSamples,
    __out int *piResult,
    LONGLONG llSumSq,
    LONGLONG iTot
)
{
    CheckPointer(piResult,E_POINTER);
    CAutoLock cVideoLock(&m_InterfaceLock);

    if (NULL==m_pClock) {
        *piResult = 0;
        return NOERROR;
    }

    // If S is the Sum of the Squares of observations and
    //    T the Total (i.e. sum) of the observations and there were
    //    N observations, then an estimate of the standard deviation is
    //      sqrt( (S - T**2/N) / (N-1) )

    if (nSamples<=1) {
        *piResult = 0;
    } else {
        LONGLONG x;
        // First frames have invalid stamps, so we get no stats for them
        // So we need 2 frames to get 1 datum, so N is cFramesDrawn-1

        // so we use m_cFramesDrawn-1 here
        x = llSumSq - llMulDiv(iTot, iTot, nSamples, 0);
        x = x / (nSamples-1);
        ASSERT(x>=0);
        *piResult = isqrt_((LONG)x);
    }
    return NOERROR;
}

// Set *piDev to the standard deviation in mSec of the sync offset
// of each frame since streaming started.

STDMETHODIMP CBaseVideoRenderer2::get_DevSyncOffset(__out int *piDev)
{
    // First frames have invalid stamps, so we get no stats for them
    // So we need 2 frames to get 1 datum, so N is cFramesDrawn-1
    return GetStdDev(m_cFramesDrawn - 1,
                     piDev,
                     m_iSumSqAcc,
                     m_iTotAcc);
} // get_DevSyncOffset


// Set *piJitter to the standard deviation in mSec of the inter-frame time
// of frames since streaming started.

STDMETHODIMP CBaseVideoRenderer2::get_Jitter(__out int *piJitter)
{
    // First frames have invalid stamps, so we get no stats for them
    // So second frame gives invalid inter-frame time
    // So we need 3 frames to get 1 datum, so N is cFramesDrawn-2
    return GetStdDev(m_cFramesDrawn - 2,
                     piJitter,
                     m_iSumSqFrameTime,
                     m_iSumFrameTime);
} // get_Jitter


// Overidden to return our IQualProp interface

STDMETHODIMP
CBaseVideoRenderer2::NonDelegatingQueryInterface(REFIID riid,__deref_out VOID **ppv)
{
    // We return IQualProp and delegate everything else

    if (riid == IID_IQualProp) {
        return GetInterface( (IQualProp *)this, ppv);
    } else if (riid == IID_IQualityControl) {
        return GetInterface( (IQualityControl *)this, ppv);
    }
    return CBaseRenderer::NonDelegatingQueryInterface(riid,ppv);
}


// Override JoinFilterGraph so that, just before leaving
// the graph we can send an EC_WINDOW_DESTROYED event

STDMETHODIMP
CBaseVideoRenderer2::JoinFilterGraph(__inout_opt IFilterGraph *pGraph, __in_opt LPCWSTR pName)
{
    // Since we send EC_ACTIVATE, we also need to ensure
    // we send EC_WINDOW_DESTROYED or the resource manager may be
    // holding us as a focus object
    if (!pGraph && m_pGraph) {

        // We were in a graph and now we're not
        // Do this properly in case we are aggregated
        IBaseFilter* pFilter = this;
        NotifyEvent(EC_WINDOW_DESTROYED, (LPARAM) pFilter, 0);
    }
    return CBaseFilter::JoinFilterGraph(pGraph, pName);
}
