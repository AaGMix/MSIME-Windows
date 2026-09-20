#include "FanyDefines.h"
#include "Private.h"
#include "TfTextLayoutSink.h"
#include "MetasequoiaIME.h"
#include "GetTextExtentEditSession.h"
#include "CaretAnchorPolicy.h"
#include <debugapi.h>
#include <fmt/xchar.h>

POINT GetPhysicalTextAnchor(_In_ ITfContextView *pContextView, _In_ const RECT &textExtent)
{
    POINT anchor = {textExtent.left, textExtent.bottom};
    HWND hostWindow = nullptr;
    if (SUCCEEDED(pContextView->GetWnd(&hostWindow)) && hostWindow)
    {
        hostWindow = GetAncestor(hostWindow, GA_ROOT);
        POINT physicalAnchor = anchor;
        if (hostWindow && LogicalToPhysicalPointForPerMonitorDPI(hostWindow, &physicalAnchor))
        {
            anchor = physicalAnchor;
        }
    }
    return anchor;
}

namespace
{
bool IsUsableTextExtent(const RECT &rect, BOOL clipped)
{
    const CaretAnchorRect extent = {rect.left, rect.top, rect.right, rect.bottom};
    return IsUsableCaretExtent(extent, clipped != FALSE);
}

enum class AdjacentExtentResult
{
    Absent,
    Boundary,
    Usable,
    Failed,
};

AdjacentExtentResult MeasureAdjacentTextExtent(ITfContextView *view, TfEditCookie ec, ITfRange *caretRange, LONG count,
                                               RECT *rect)
{
    ITfRange *range = nullptr;
    if (FAILED(caretRange->Clone(&range)) || !range)
        return AdjacentExtentResult::Failed;

    LONG shifted = 0;
    const HRESULT shiftResult =
        count < 0 ? range->ShiftStart(ec, count, &shifted, nullptr) : range->ShiftEnd(ec, count, &shifted, nullptr);
    if (FAILED(shiftResult))
    {
        range->Release();
        return AdjacentExtentResult::Failed;
    }
    if (shifted != count)
    {
        range->Release();
        return AdjacentExtentResult::Absent;
    }

    WCHAR text[2]{};
    ULONG fetched = 0;
    if (FAILED(range->GetText(ec, 0, text, 2, &fetched)) || fetched == 0)
    {
        range->Release();
        return AdjacentExtentResult::Failed;
    }
    if (text[0] == L'\r' || text[0] == L'\n' || text[0] == 0x2028 || text[0] == 0x2029)
    {
        range->Release();
        return AdjacentExtentResult::Boundary;
    }

    BOOL clipped = TRUE;
    const HRESULT extentResult = view->GetTextExt(ec, range, rect, &clipped);
    range->Release();
    return SUCCEEDED(extentResult) && IsUsableTextExtent(*rect, clipped) ? AdjacentExtentResult::Usable
                                                                         : AdjacentExtentResult::Failed;
}
} // namespace

bool GetCollapsedSelectionPhysicalAnchor(ITfContext *pContext, TfEditCookie ec, const TF_SELECTION &selection,
                                         POINT *anchor)
{
    if (!pContext || !selection.range || !anchor)
        return false;

    ITfRange *caretRange = nullptr;
    if (FAILED(selection.range->Clone(&caretRange)) || !caretRange)
        return false;

    const TfAnchor caretAnchor = selection.style.ase == TF_AE_START ? TF_ANCHOR_START : TF_ANCHOR_END;
    if (FAILED(caretRange->Collapse(ec, caretAnchor)))
    {
        caretRange->Release();
        return false;
    }

    ITfContextView *view = nullptr;
    if (FAILED(pContext->GetActiveView(&view)) || !view)
    {
        caretRange->Release();
        return false;
    }

    RECT previousRect{};
    RECT nextRect{};
    const AdjacentExtentResult previous = MeasureAdjacentTextExtent(view, ec, caretRange, -1, &previousRect);
    const AdjacentExtentResult next = MeasureAdjacentTextExtent(view, ec, caretRange, 1, &nextRect);

    bool resolved = false;
    RECT caretRect{};
    if (previous != AdjacentExtentResult::Failed && next != AdjacentExtentResult::Failed &&
        (previous == AdjacentExtentResult::Usable || next == AdjacentExtentResult::Usable))
    {
        const CaretAnchorRect previousAnchor = {previousRect.left, previousRect.top, previousRect.right,
                                                previousRect.bottom};
        const CaretAnchorRect nextAnchor = {nextRect.left, nextRect.top, nextRect.right, nextRect.bottom};
        CaretAnchorPoint selected{};
        if (SelectAdjacentCaretAnchor(previous == AdjacentExtentResult::Usable ? &previousAnchor : nullptr,
                                      next == AdjacentExtentResult::Usable ? &nextAnchor : nullptr, &selected))
        {
            caretRect = {selected.x, selected.y, selected.x, selected.y};
            resolved = true;
        }
    }
    else if (previous == AdjacentExtentResult::Absent && next == AdjacentExtentResult::Absent)
    {
        BOOL clipped = TRUE;
        if (SUCCEEDED(view->GetTextExt(ec, caretRange, &caretRect, &clipped)) && IsUsableTextExtent(caretRect, clipped))
        {
            resolved = true;
        }
    }

    if (resolved)
        *anchor = GetPhysicalTextAnchor(view, caretRect);

    view->Release();
    caretRange->Release();
    return resolved;
}

CTfTextLayoutSink::CTfTextLayoutSink(_In_ CMetasequoiaIME *pTextService)
{
    _pTextService = pTextService;
    _pTextService->AddRef();

    _pRangeComposition = nullptr;
    _pContextDocument = nullptr;
    _tfEditCookie = TF_INVALID_EDIT_COOKIE;

    _dwCookieTextLayoutSink = TF_INVALID_COOKIE;
    _hasValidAnchor = false;

    _refCount = 1;

    DllAddRef();
}

CTfTextLayoutSink::~CTfTextLayoutSink()
{
    if (_pTextService)
    {
        _pTextService->Release();
    }

    DllRelease();
}

STDAPI CTfTextLayoutSink::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfTextLayoutSink))
    {
        *ppvObj = (ITfTextLayoutSink *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDAPI_(ULONG) CTfTextLayoutSink::AddRef()
{
    return ++_refCount;
}

STDAPI_(ULONG) CTfTextLayoutSink::Release()
{
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// ITfTextLayoutSink::OnLayoutChange
//
//----------------------------------------------------------------------------

STDAPI CTfTextLayoutSink::OnLayoutChange(_In_ ITfContext *pContext, TfLayoutCode lcode,
                                         _In_ ITfContextView *pContextView)
{
    // we're interested in only document context.
    if (pContext != _pContextDocument)
    {
        return S_OK;
    }

    switch (lcode)
    {
    case TF_LC_CREATE: {
    }
    case TF_LC_CHANGE: {
        CGetTextExtentEditSession *pEditSession = nullptr;
        pEditSession = new (std::nothrow)
            CGetTextExtentEditSession(_pTextService, pContext, pContextView, _pRangeComposition, this);
        if (nullptr != (pEditSession))
        {
            HRESULT hr = S_OK;
            pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_SYNC | TF_ES_READ, &hr);
            pEditSession->Release();
        }
    }
    break;

    case TF_LC_DESTROY:
        _LayoutDestroyNotification();
        break;
    }
    return S_OK;
}

HRESULT CTfTextLayoutSink::_StartLayout(_In_ ITfContext *pContextDocument, TfEditCookie ec,
                                        _In_ ITfRange *pRangeComposition)
{
    _pContextDocument = pContextDocument;
    _pContextDocument->AddRef();

    _pRangeComposition = pRangeComposition;
    _pRangeComposition->AddRef();

    _tfEditCookie = ec;
    _hasValidAnchor = false;
    HRESULT hr = _AdviseTextLayoutSink();
    return hr;
}

VOID CTfTextLayoutSink::_EndLayout()
{
    _hasValidAnchor = false;

    if (_pRangeComposition)
    {
        _pRangeComposition->Release();
        _pRangeComposition = nullptr;
    }

    if (_pContextDocument)
    {
        _UnadviseTextLayoutSink();
        _pContextDocument->Release();
        _pContextDocument = nullptr;
    }
}

HRESULT CTfTextLayoutSink::_AdviseTextLayoutSink()
{
    HRESULT hr = S_OK;
    ITfSource *pSource = nullptr;

    hr = _pContextDocument->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink *)this, &_dwCookieTextLayoutSink);
    if (FAILED(hr))
    {
        pSource->Release();
        return hr;
    }

    pSource->Release();

    return hr;
}

HRESULT CTfTextLayoutSink::_UnadviseTextLayoutSink()
{
    HRESULT hr = S_OK;
    ITfSource *pSource = nullptr;

    if (nullptr == _pContextDocument)
    {
        return E_FAIL;
    }

    hr = _pContextDocument->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->UnadviseSink(_dwCookieTextLayoutSink);
    if (FAILED(hr))
    {
        pSource->Release();
        return hr;
    }

    pSource->Release();
    _dwCookieTextLayoutSink = TF_INVALID_COOKIE;

    return hr;
}

/**
 * @brief 获取 caret 的坐标
 *
 * 本质上是通过 _pContextView->GetTextExt 获取 caret 坐标，
 * 因此，涉及到 caret 坐标的地方不止这里，还有其他地方，是直接
 * 使用 _pContextView->GetTextExt 来获取坐标的。
 *
 * @param lpRect
 * @return HRESULT
 */
HRESULT CTfTextLayoutSink::_GetTextExt(_Out_ RECT *lpRect, _Out_ POINT *lpAnchor)
{
    HRESULT hr = S_OK;
    BOOL isClipped = TRUE;
    ITfContextView *pContextView = nullptr;

    hr = _pContextDocument->GetActiveView(&pContextView);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pContextView->GetTextExt(_tfEditCookie, _pRangeComposition, lpRect, &isClipped);

    // TS_E_NOLAYOUT means "the host has not laid this range out yet, ask again",
    // not "the caret is gone". Chromium-based hosts (Electron apps, browsers)
    // answer it routinely while their renderer is behind, and far more often
    // while the machine is busy. Adopting the off-screen sentinel here parks the
    // candidate window at INVALID_Y for one frame and the next measurement pulls
    // it back — that is the flicker seen under load. Report the failure so the
    // caller keeps the anchor it already has; TF_LC_CHANGE delivers the real
    // position as soon as layout completes. Only do this once this layout
    // session has produced a good anchor, so composition start still falls back
    // to the sentinel rather than reusing a stale one from another document.
    if (hr == TS_E_NOLAYOUT && _hasValidAnchor)
    {
        pContextView->Release();
        if (Global::TsfDiagnosticLogEnabled.load(std::memory_order_relaxed))
        {
            QueueTsfDiagnosticLog(L"[candidate-layout] kept last anchor across transient TS_E_NOLAYOUT");
        }
        return hr;
    }

    if (FAILED(hr))
    {
        // Set default value to make sure the window is hidden by moving it out of the screen
        lpRect->left = 0;
        lpRect->bottom = Global::INVALID_Y;
        *lpAnchor = {0, Global::INVALID_Y};
    }
    else if (isClipped && lpRect && (lpRect->right <= lpRect->left || lpRect->bottom <= lpRect->top))
    {
        // Office often reports isClipped=TRUE with a still-usable rect; only reject
        // degenerate clipped extents that would anchor the candidate off-screen.
        lpRect->left = 0;
        lpRect->bottom = Global::INVALID_Y;
        *lpAnchor = {0, Global::INVALID_Y};
    }
    else
    {
        *lpAnchor = GetPhysicalTextAnchor(pContextView, *lpRect);
        _hasValidAnchor = true;
    }
    pContextView->Release();

    return S_OK;
}
