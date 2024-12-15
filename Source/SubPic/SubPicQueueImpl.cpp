/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2024 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include <chrono>
#include <intsafe.h>
#include "Utils/Util.h"
#include "SubPicQueueImpl.h"

#define SUBPIC_TRACE_LEVEL 0

//
// CSubPicQueueImpl
//
const double CSubPicQueueImpl::DEFAULT_FPS = 24/1.001;

CSubPicQueueImpl::CSubPicQueueImpl(ISubPicAllocator* pAllocator, HRESULT* phr)
	: CUnknown(L"CSubPicQueueImpl", nullptr)
	, m_pAllocator(pAllocator)
{
	if (phr) {
		*phr = S_OK;
	}

	if (!m_pAllocator) {
		if (phr) {
			*phr = E_FAIL;
		}
		return;
	}
}

CSubPicQueueImpl::~CSubPicQueueImpl()
{
}

STDMETHODIMP CSubPicQueueImpl::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	return
		QI(ISubPicQueue)
		__super::NonDelegatingQueryInterface(riid, ppv);
}

// ISubPicQueue

STDMETHODIMP CSubPicQueueImpl::SetSubPicProvider(ISubPicProvider* pSubPicProvider)
{
	CAutoLock cAutoLock(&m_csSubPicProvider);

	m_pSubPicProviderWithSharedLock = std::make_shared<SubPicProviderWithSharedLock>(pSubPicProvider);

	Invalidate();

	return S_OK;
}

STDMETHODIMP CSubPicQueueImpl::GetSubPicProvider(ISubPicProvider** pSubPicProvider)
{
	CheckPointer(pSubPicProvider, E_POINTER);

	CAutoLock cAutoLock(&m_csSubPicProvider);

	if (m_pSubPicProviderWithSharedLock && m_pSubPicProviderWithSharedLock->pSubPicProvider) {
		*pSubPicProvider = m_pSubPicProviderWithSharedLock->pSubPicProvider;
		(*pSubPicProvider)->AddRef();
	}

	return *pSubPicProvider ? S_OK : E_FAIL;
}

STDMETHODIMP CSubPicQueueImpl::SetFPS(double fps)
{
	m_fps = fps;

	return S_OK;
}

STDMETHODIMP CSubPicQueueImpl::SetTime(REFERENCE_TIME rtNow)
{
	m_rtNow = rtNow;

	return S_OK;
}

// private

HRESULT CSubPicQueueImpl::RenderTo(ISubPic* pSubPic, REFERENCE_TIME rtStart, REFERENCE_TIME rtStop, double fps, BOOL bIsAnimated)
{
	CheckPointer(pSubPic, E_POINTER);

	HRESULT hr = E_FAIL;

	CComPtr<ISubPicProvider> pSubPicProvider;
	if (FAILED(GetSubPicProvider(&pSubPicProvider)) || !pSubPicProvider) {
		return hr;
	}

	hr = pSubPic->ClearDirtyRect();

	SubPicDesc spd;
	if (SUCCEEDED(hr)) {
		hr = pSubPic->Lock(spd);
	}
	if (SUCCEEDED(hr)) {
		CRect r(0,0,0,0);
		REFERENCE_TIME rtRender = rtStart;
		if (bIsAnimated) {
			// This is some sort of hack to avoid rendering the wrong frame
			// when the start time is slightly mispredicted by the queue
			rtRender = (rtStart + rtStop) / 2;
		} else {
			rtRender += (rtStop - rtStart - 1);
		}
		hr = pSubPicProvider->Render(spd, rtRender, fps, r);

		pSubPic->SetStart(rtStart);
		pSubPic->SetStop(rtStop);

		pSubPic->Unlock(r);
	}

	return hr;
}

//
// CSubPicQueue
//

CSubPicQueue::CSubPicQueue(int nMaxSubPic, bool bDisableAnim, bool bAllowDropSubPic, ISubPicAllocator* pAllocator, HRESULT* phr)
	: CSubPicQueueImpl(pAllocator, phr)
	, m_nMaxSubPic(nMaxSubPic)
	, m_bDisableAnim(bDisableAnim)
	, m_bAllowDropSubPic(bAllowDropSubPic)
{
	if (phr && FAILED(*phr)) {
		return;
	}

	if (m_nMaxSubPic < 1) {
		if (phr) {
			*phr = E_INVALIDARG;
		}
		return;
	}

	CAMThread::Create();
}

CSubPicQueue::~CSubPicQueue()
{
	m_bExitThread = true;
	SetSubPicProvider(nullptr);
	CAMThread::Close();
}

// ISubPicQueue

STDMETHODIMP CSubPicQueue::SetFPS(double fps)
{
	HRESULT hr = __super::SetFPS(fps);
	if (FAILED(hr)) {
		return hr;
	}

	m_rtTimePerFrame = std::llround(10000000.0 / m_fps);

	m_runQueueEvent.Set();

	return S_OK;
}

STDMETHODIMP CSubPicQueue::SetTime(REFERENCE_TIME rtNow)
{
	HRESULT hr = __super::SetTime(rtNow);
	if (FAILED(hr)) {
		return hr;
	}

	// We want the queue to stay sorted so if we seek in the past, we invalidate
	if (m_rtNowLast >= 0 && m_rtNowLast - m_rtNow >= m_rtTimePerFrame) {
		Invalidate(m_rtNow);
	}

	m_rtNowLast = m_rtNow;

	m_runQueueEvent.Set();

	return S_OK;
}

STDMETHODIMP CSubPicQueue::Invalidate(REFERENCE_TIME rtInvalidate)
{
	std::unique_lock<std::mutex> lock(m_mutexQueue);

#if SUBPIC_TRACE_LEVEL > 0
	DLog(L"Invalidate: %f", double(rtInvalidate) / 10000000.0);
#endif

	m_bInvalidate = true;
	m_rtInvalidate = rtInvalidate;
	m_rtNowLast = LONGLONG_ERROR;

	{
		std::lock_guard<std::mutex> lock(m_mutexSubpic);
		if (m_pSubPic && m_pSubPic->GetStop() > rtInvalidate) {
			m_pSubPic.Release();
		}
	}

	while (m_queue.size() && m_queue.back()->GetStop() > rtInvalidate) {
#if SUBPIC_TRACE_LEVEL > 2
		const CComPtr<ISubPic>& pSubPic = m_queue.back();
		REFERENCE_TIME rtStart = pSubPic->GetStart();
		REFERENCE_TIME rtStop = pSubPic->GetStop();
		REFERENCE_TIME rtSegmentStop = pSubPic->GetSegmentStop();
		DLog(L"  %f -> %f -> %f", double(rtStart) / 10000000.0, double(rtStop) / 10000000.0, double(rtSegmentStop) / 10000000.0);
#endif
		m_queue.pop_back();
	}

	// If we invalidate in the past, always give the queue a chance to re-render the modified subtitles
	if (rtInvalidate >= 0 && rtInvalidate < m_rtNow) {
		m_rtNow = rtInvalidate;
	}

	lock.unlock();
	m_condQueueFull.notify_one();
	m_runQueueEvent.Set();

	return S_OK;
}

STDMETHODIMP_(bool) CSubPicQueue::LookupSubPic(REFERENCE_TIME rtNow, CComPtr<ISubPic>& ppSubPic)
{
	// Old version of LookupSubPic, keep legacy behavior and never try to block
	return LookupSubPic(rtNow, false, ppSubPic);
}

STDMETHODIMP_(bool) CSubPicQueue::LookupSubPic(REFERENCE_TIME rtNow, bool bAdviseBlocking, CComPtr<ISubPic>& ppSubPic)
{
	bool bStopSearch = false;

	{
		std::lock_guard<std::mutex> lock(m_mutexSubpic);

		// See if we can reuse the latest subpic
		if (m_pSubPic) {
			REFERENCE_TIME rtSegmentStart = m_pSubPic->GetSegmentStart();
			REFERENCE_TIME rtSegmentStop = m_pSubPic->GetSegmentStop();

			if (rtSegmentStart <= rtNow && rtNow < rtSegmentStop) {
				ppSubPic = m_pSubPic;

				REFERENCE_TIME rtStart = m_pSubPic->GetStart();
				REFERENCE_TIME rtStop = m_pSubPic->GetStop();

				if (rtStart <= rtNow && rtNow < rtStop) {
#if SUBPIC_TRACE_LEVEL > 2
					DLog(L"LookupSubPic: Exact match on the latest subpic");
#endif
					bStopSearch = true;
				} else {
#if SUBPIC_TRACE_LEVEL > 2
					DLog(L"LookupSubPic: Possible match on the latest subpic");
#endif
				}
			} else if (rtSegmentStop <= rtNow) {
				m_pSubPic.Release();
			}
		}
	}

	bool bTryBlocking = bAdviseBlocking || !m_bAllowDropSubPic;
	while (!bStopSearch) {
		// Look for the subpic in the queue
		{
			std::unique_lock<std::mutex> lock(m_mutexQueue);

#if SUBPIC_TRACE_LEVEL > 2
			DLog(L"LookupSubPic: Searching the queue");
#endif

			while (m_queue.size() && !bStopSearch) {
				const CComPtr<ISubPic>& pSubPic = m_queue.front();
				REFERENCE_TIME rtSegmentStart = pSubPic->GetSegmentStart();

				if (rtSegmentStart > rtNow) {
#if SUBPIC_TRACE_LEVEL > 2
					DLog(L"rtSegmentStart > rtNow, stopping the search");
#endif
					bStopSearch = true;
				} else { // rtSegmentStart <= rtNow
					bool bRemoveFromQueue = true;
					REFERENCE_TIME rtStart = pSubPic->GetStart();
					REFERENCE_TIME rtStop = pSubPic->GetStop();
					REFERENCE_TIME rtSegmentStop = pSubPic->GetSegmentStop();

					if (rtSegmentStop <= rtNow) {
#if SUBPIC_TRACE_LEVEL > 2
						DLog(L"Removing old subpic (rtNow=%f): %f -> %f -> %f",
							  double(rtNow) / 10000000.0, double(rtStart) / 10000000.0,
							  double(rtStop) / 10000000.0, double(rtSegmentStop) / 10000000.0);
#endif
					} else { // rtNow < rtSegmentStop
						if (rtStart <= rtNow && rtNow < rtStop) {
#if SUBPIC_TRACE_LEVEL > 2
							DLog(L"Exact match found in the queue");
#endif
							ppSubPic = pSubPic;
							bStopSearch = true;
						} else if (rtNow >= rtStop) {
							// Reuse old subpic
							ppSubPic = pSubPic;
						} else { // rtNow < rtStart
							if (!ppSubPic || ppSubPic->GetStop() <= rtNow) {
								// Should be really rare that we use a subpic in advance
								// unless we mispredicted the timing slightly
								ppSubPic = pSubPic;
							} else {
								bRemoveFromQueue = false;
							}
							bStopSearch = true;
						}
					}

					if (bRemoveFromQueue) {
						m_queue.pop_front();
					}
				}
			}

			lock.unlock();
			m_condQueueFull.notify_one();
		}

		// If we didn't get any subpic yet and blocking is advised, just try harder to get one
		if (!ppSubPic && bTryBlocking) {
			bTryBlocking = false;
			bStopSearch = true;

			auto pSubPicProviderWithSharedLock = GetSubPicProviderWithSharedLock();
			if (pSubPicProviderWithSharedLock && SUCCEEDED(pSubPicProviderWithSharedLock->Lock())) {
				auto& pSubPicProvider = pSubPicProviderWithSharedLock->pSubPicProvider;
				double fps = m_fps;
				if (POSITION pos = pSubPicProvider->GetStartPosition(rtNow, fps)) {
					REFERENCE_TIME rtStart = pSubPicProvider->GetStart(pos, fps);
					REFERENCE_TIME rtStop = pSubPicProvider->GetStop(pos, fps);
					if (rtStart <= rtNow && rtNow < rtStop) {
						bStopSearch = false;
					}
				}
				pSubPicProviderWithSharedLock->Unlock();

				if (!bStopSearch) {
					std::unique_lock<std::mutex> lock(m_mutexQueue);

					auto queueReady = [this, rtNow]() {
						return ((int)m_queue.size() == m_nMaxSubPic)
							   || (m_queue.size() && m_queue.back()->GetStop() > rtNow);
					};

					auto duration = bAdviseBlocking ? std::chrono::milliseconds(m_rtTimePerFrame / 10000) : std::chrono::seconds(1);
					m_condQueueReady.wait_for(lock, duration, queueReady);
				}
			}
		} else {
			bStopSearch = true;
		}
	}

	if (ppSubPic) {
		// Save the subpic for later reuse
		std::lock_guard<std::mutex> lock(m_mutexSubpic);
		m_pSubPic = ppSubPic;

#if SUBPIC_TRACE_LEVEL > 0
		REFERENCE_TIME rtStart = ppSubPic->GetStart();
		REFERENCE_TIME rtStop = ppSubPic->GetStart();
		REFERENCE_TIME rtSegmentStop = ppSubPic->GetSegmentStop();
		CRect r;
		ppSubPic->GetDirtyRect(&r);
		DLog(L"Display at %f: %f -> %f -> %f (%dx%d)",
			  double(rtNow) / 10000000.0, double(rtStart) / 10000000.0, double(rtStop) / 10000000.0, double(rtSegmentStop) / 10000000.0,
			  r.Width(), r.Height());
#endif
	} else {
#if SUBPIC_TRACE_LEVEL > 1
		DLog(L"No subpicture to display at %f", double(rtNow) / 10000000.0);
#endif
	}

	return !!ppSubPic;
}

STDMETHODIMP CSubPicQueue::GetStats(int& nSubPics, REFERENCE_TIME& rtNow, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	std::lock_guard<std::mutex> lock(m_mutexQueue);

	nSubPics = (int)m_queue.size();
	rtNow = m_rtNow;
	if (nSubPics) {
		rtStart = m_queue.front()->GetStart();
		rtStop = m_queue.back()->GetStop();
	} else {
		rtStart = rtStop = 0;
	}

	return S_OK;
}

STDMETHODIMP CSubPicQueue::GetStats(int nSubPic, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	std::lock_guard<std::mutex> lock(m_mutexQueue);

	HRESULT hr = E_INVALIDARG;

	if (nSubPic >= 0 && nSubPic < (int)m_queue.size()) {
		auto it = m_queue.cbegin();
		std::advance(it, nSubPic);
		rtStart = (*it)->GetStart();
		rtStop  = (*it)->GetStop();
		hr = S_OK;
	} else {
		rtStart = rtStop = -1;
	}

	return hr;
}

// private

bool CSubPicQueue::EnqueueSubPic(CComPtr<ISubPic>& pSubPic, bool bBlocking)
{
	auto canAddToQueue = [this]() {
		return (int)m_queue.size() < m_nMaxSubPic;
	};

	bool bAdded = false;

	std::unique_lock<std::mutex> lock(m_mutexQueue);
	if (bBlocking) {
		// Wait for enough room in the queue
		m_condQueueFull.wait(lock, canAddToQueue);
	}

	if (canAddToQueue()) {
		if (m_bInvalidate && pSubPic->GetStop() > m_rtInvalidate) {
#if SUBPIC_TRACE_LEVEL > 1
			DLog(L"Subtitle Renderer Thread: Dropping rendered subpic because of invalidation");
#endif
		} else {
			m_queue.emplace_back(pSubPic);
			lock.unlock();
			m_condQueueReady.notify_one();
			bAdded = true;
		}
		pSubPic.Release();
	}

	return bAdded;
}

REFERENCE_TIME CSubPicQueue::GetCurrentRenderingTime()
{
	REFERENCE_TIME rtNow = -1;

	{
		std::lock_guard<std::mutex> lock(m_mutexQueue);

		if (m_queue.size()) {
			rtNow = m_queue.back()->GetStop();
		}
	}

	return std::max(rtNow, m_rtNow);
}

// overrides

DWORD CSubPicQueue::ThreadProc()
{
	bool bDisableAnim = m_bDisableAnim;
	SetThreadName(DWORD(-1), "Subtitle Renderer Thread");
	SetThreadPriority(m_hThread, bDisableAnim ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_ABOVE_NORMAL);

	bool bWaitForEvent = false;
	for (; !m_bExitThread;) {
		// When we have nothing to render, we just wait a bit
		if (bWaitForEvent) {
			bWaitForEvent = false;
			m_runQueueEvent.Wait();
		}

		auto pSubPicProviderWithSharedLock = GetSubPicProviderWithSharedLock();
		if (pSubPicProviderWithSharedLock && SUCCEEDED(pSubPicProviderWithSharedLock->Lock())) {
			auto& pSubPicProvider = pSubPicProviderWithSharedLock->pSubPicProvider;
			double fps = m_fps;
			REFERENCE_TIME rtTimePerFrame = m_rtTimePerFrame;
			m_bInvalidate = false;
			CComPtr<ISubPic> pSubPic;

			SUBTITLE_TYPE sType = pSubPicProvider->GetType();

			REFERENCE_TIME rtStartRendering = GetCurrentRenderingTime();
			POSITION pos = pSubPicProvider->GetStartPosition(rtStartRendering, fps);
			if (!pos) {
				bWaitForEvent = true;
			}
			for (; pos; pos = pSubPicProvider->GetNext(pos)) {
				REFERENCE_TIME rtStart = pSubPicProvider->GetStart(pos, fps);
				REFERENCE_TIME rtStop = pSubPicProvider->GetStop(pos, fps);

				// We are already one minute ahead, this should be enough
				if (rtStart >= m_rtNow + 60 * 10000000i64) {
					bWaitForEvent = true;
					break;
				}

				REFERENCE_TIME rtCurrent = std::max(rtStart, rtStartRendering);
				if (rtCurrent > m_rtNow && rtTimePerFrame <= rtStop - rtStart) {
					// Round current time to the next estimated video frame timing
					REFERENCE_TIME rtCurrentRounded = (rtCurrent / rtTimePerFrame) * rtTimePerFrame;
					if (rtCurrentRounded < rtCurrent) {
						rtCurrent = rtCurrentRounded + rtTimePerFrame;
					}
				} else {
					rtCurrent = m_rtNow;
				}

				// Check that we aren't late already...
				if (rtCurrent < rtStop) {
					bool bIsAnimated = pSubPicProvider->IsAnimated(pos) && !bDisableAnim;
					bool bStopRendering = false;

					while (rtCurrent < rtStop) {
						SIZE	maxTextureSize, virtualSize;
						POINT   virtualTopLeft;
						HRESULT hr2;

						if (SUCCEEDED(hr2 = pSubPicProvider->GetTextureSize(pos, maxTextureSize, virtualSize, virtualTopLeft))) {
							m_pAllocator->SetMaxTextureSize(maxTextureSize);
						}

						CComPtr<ISubPic> pStatic;
						if (FAILED(m_pAllocator->GetStatic(&pStatic))) {
							break;
						}

						REFERENCE_TIME rtStopReal;
						if (rtStop == ISubPicProvider::UNKNOWN_TIME) { // Special case for subtitles with unknown end time
							// Force a one frame duration
							rtStopReal = rtCurrent + rtTimePerFrame;
						} else {
							rtStopReal = rtStop;
						}

						HRESULT hr;
						if (bIsAnimated) {
							// 3/4 is a magic number we use to avoid reusing the wrong frame due to slight
							// misprediction of the frame end time
							hr = RenderTo(pStatic, rtCurrent, std::min(rtCurrent + rtTimePerFrame * 3 / 4, rtStopReal), fps, bIsAnimated);
							// Set the segment start and stop timings
							pStatic->SetSegmentStart(rtStart);
							// The stop timing can be moved so that the duration from the current start time
							// of the subpic to the segment end is always at least one video frame long. This
							// avoids missing subtitle frame due to rounding errors in the timings.
							// At worst this can cause a segment to be displayed for one more frame than expected
							// but it's much less annoying than having the subtitle disappearing for one frame
							pStatic->SetSegmentStop(std::max(rtCurrent + rtTimePerFrame, rtStopReal));
							rtCurrent = std::min(rtCurrent + rtTimePerFrame, rtStopReal);
						} else {
							hr = RenderTo(pStatic, rtStart, rtStopReal, fps, bIsAnimated);
							// Non-animated subtitles aren't part of a segment
							pStatic->SetSegmentStart(ISubPic::INVALID_SUBPIC_TIME);
							pStatic->SetSegmentStop(ISubPic::INVALID_SUBPIC_TIME);
							rtCurrent = rtStopReal;
						}

						if (FAILED(hr)) {
							break;
						}

#if SUBPIC_TRACE_LEVEL > 1
						CRect r;
						pStatic->GetDirtyRect(&r);
						DLog(L"Subtitle Renderer Thread: Render %f -> %f -> %f -> %f (%dx%d)",
							  double(rtStart) / 10000000.0, double(pStatic->GetStart()) / 10000000.0,
							  double(pStatic->GetStop()) / 10000000.0, double(rtStop) / 10000000.0,
							  r.Width(), r.Height());
#endif

						pSubPic.Release();
						if (FAILED(m_pAllocator->AllocDynamic(&pSubPic))
								|| FAILED(pStatic->CopyTo(pSubPic))) {
							break;
						}

						if (SUCCEEDED(hr2)) {
							pSubPic->SetVirtualTextureSize(virtualSize, virtualTopLeft);
						}

						pSubPic->SetType(sType);

						// Try to enqueue the subpic, if the queue is full stop rendering
						if (!EnqueueSubPic(pSubPic, false)) {
							bStopRendering = true;
							break;
						}

						if (m_rtNow > rtCurrent) {
#if SUBPIC_TRACE_LEVEL > 0
							DLog(L"Subtitle Renderer Thread: the queue is late, trying to catch up...");
#endif
							rtCurrent = m_rtNow;
						}
					}

					if (bStopRendering) {
						break;
					}
				} else {
#if SUBPIC_TRACE_LEVEL > 0
					DLog(L"Subtitle Renderer Thread: the queue is late, trying to catch up...");
#endif
				}
			}

			pSubPicProviderWithSharedLock->Unlock();

			// If we couldn't enqueue the subpic before, wait for some room in the queue
			// but unsure to unlock the subpicture provider first to avoid deadlocks
			if (pSubPic) {
				EnqueueSubPic(pSubPic, true);
			}
		} else {
			bWaitForEvent = true;
		}
	}

	return 0;
}

//
// CSubPicQueueNoThread
//

CSubPicQueueNoThread::CSubPicQueueNoThread(bool bDisableAnim, ISubPicAllocator* pAllocator, HRESULT* phr)
	: CSubPicQueueImpl(pAllocator, phr)
	, m_bDisableAnim(bDisableAnim)
{
}

CSubPicQueueNoThread::~CSubPicQueueNoThread()
{
}

// ISubPicQueue

STDMETHODIMP CSubPicQueueNoThread::Invalidate(REFERENCE_TIME rtInvalidate)
{
	CAutoLock cQueueLock(&m_csLock);

	if (m_pSubPic && m_pSubPic->GetStop() > rtInvalidate) {
		m_pSubPic.Release();
	}

	return S_OK;
}

STDMETHODIMP_(bool) CSubPicQueueNoThread::LookupSubPic(REFERENCE_TIME rtNow, CComPtr<ISubPic>& ppSubPic)
{
	// CSubPicQueueNoThread is always blocking so bAdviseBlocking doesn't matter anyway
	return LookupSubPic(rtNow, true, ppSubPic);
}

STDMETHODIMP_(bool) CSubPicQueueNoThread::LookupSubPic(REFERENCE_TIME rtNow, bool /*bAdviseBlocking*/, CComPtr<ISubPic>& ppSubPic)
{
	// CSubPicQueueNoThread is always blocking so we ignore bAdviseBlocking

	CComPtr<ISubPic> pSubPic;

	{
		CAutoLock cAutoLock(&m_csLock);

		pSubPic = m_pSubPic;
	}

	if (pSubPic && pSubPic->GetStart() <= rtNow && rtNow < pSubPic->GetStop()) {
		ppSubPic = pSubPic;
	} else {
		CComPtr<ISubPicProvider> pSubPicProvider;
		if (SUCCEEDED(GetSubPicProvider(&pSubPicProvider)) && pSubPicProvider
				&& SUCCEEDED(pSubPicProvider->Lock())) {
			SUBTITLE_TYPE sType = pSubPicProvider->GetType();
			double fps = m_fps;
			POSITION pos = pSubPicProvider->GetStartPosition(rtNow, fps);
			if (pos) {
				REFERENCE_TIME rtStart;
				REFERENCE_TIME rtStop = pSubPicProvider->GetStop(pos, fps);
				bool bAnimated = pSubPicProvider->IsAnimated(pos) && !m_bDisableAnim;

				// Special case for subtitles with unknown end time
				if (rtStop == ISubPicProvider::UNKNOWN_TIME) {
					// Force a one frame duration
					rtStop = rtNow + 1;
				}

				if (bAnimated) {
					rtStart = rtNow;
					rtStop = std::min(rtNow + 1, rtStop);
				} else {
					rtStart = pSubPicProvider->GetStart(pos, fps);
				}

				if (rtStart <= rtNow && rtNow < rtStop) {
					bool	bAllocSubPic = !pSubPic;
					SIZE	maxTextureSize, virtualSize;
					POINT   virtualTopLeft;
					HRESULT hr;
					if (SUCCEEDED(hr = pSubPicProvider->GetTextureSize(pos, maxTextureSize, virtualSize, virtualTopLeft))) {
						m_pAllocator->SetMaxTextureSize(maxTextureSize);
						if (!bAllocSubPic) {
							// Ensure the previously allocated subpic is big enough to hold the subtitle to be rendered
							SIZE maxSize;
							bAllocSubPic = FAILED(pSubPic->GetMaxSize(&maxSize)) || maxSize.cx < maxTextureSize.cx || maxSize.cy < maxTextureSize.cy;
						}
					}

					if (bAllocSubPic) {
						CAutoLock cAutoLock(&m_csLock);

						m_pSubPic.Release();

						if (FAILED(m_pAllocator->AllocDynamic(&m_pSubPic))) {
							return false;
						}

						pSubPic = m_pSubPic;
					}

					if (m_pAllocator->IsDynamicWriteOnly()) {
						CComPtr<ISubPic> pStatic;
						if (SUCCEEDED(m_pAllocator->GetStatic(&pStatic))
								&& SUCCEEDED(RenderTo(pStatic, rtStart, rtStop, fps, bAnimated))
								&& SUCCEEDED(pStatic->CopyTo(pSubPic))) {
							ppSubPic = pSubPic;
						}
					} else {
						if (SUCCEEDED(RenderTo(pSubPic, rtStart, rtStop, fps, bAnimated))) {
							ppSubPic = pSubPic;
						}
					}

					if (ppSubPic) {
						if (SUCCEEDED(hr)) {
							ppSubPic->SetVirtualTextureSize(virtualSize, virtualTopLeft);
						}

						pSubPic->SetType(sType);
					}
				}
			}

			pSubPicProvider->Unlock();
		}
	}

	return !!ppSubPic;
}

STDMETHODIMP CSubPicQueueNoThread::GetStats(int& nSubPics, REFERENCE_TIME& rtNow, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	CAutoLock cAutoLock(&m_csLock);

	rtNow = m_rtNow;

	if (m_pSubPic) {
		nSubPics = 1;
		rtStart = m_pSubPic->GetStart();
		rtStop = m_pSubPic->GetStop();
	} else {
		nSubPics = 0;
		rtStart = rtStop = 0;
	}

	return S_OK;
}

STDMETHODIMP CSubPicQueueNoThread::GetStats(int nSubPic, REFERENCE_TIME& rtStart, REFERENCE_TIME& rtStop)
{
	CAutoLock cAutoLock(&m_csLock);

	if (!m_pSubPic || nSubPic != 0) {
		return E_INVALIDARG;
	}

	rtStart = m_pSubPic->GetStart();
	rtStop = m_pSubPic->GetStop();

	return S_OK;
}
