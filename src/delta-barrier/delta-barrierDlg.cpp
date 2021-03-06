// delta-barrierDlg.cpp : implementation file
//

#include "stdafx.h"
#include "delta-barrier.h"
#include "delta-barrierDlg.h"
#include "afxdialogex.h"

#include <vector>

#include <util/common/math/common.h>
#include <util/common/math/dsolve.h>

#include "model.h"

using namespace plot;
using namespace util;
using namespace math;
using namespace model;

using points_t = std::vector < point < double > > ;
using plot_t = simple_list_plot < points_t > ;

const size_t n_points = 1000;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define WM_INVOKE WM_USER + 1234

plot_t barrier_plot, wavefunc_plot, wavefunc_re_plot, wavefunc_im_plot, transmission_plot;

// CDeltaBarrierDlg dialog

UINT SimulationThreadProc(LPVOID pParam)
{
    CDeltaBarrierDlg & dlg = * (CDeltaBarrierDlg *) pParam;

    dlg.Invoke([&] () {
        dlg.DrawBarrier();
    });

    transmission_plot.data->clear();

    transmission_plot.static_world->xmin = dlg.m_dE1;
    transmission_plot.static_world->xmax = dlg.m_dE2;

    double a = (dlg.m_nN == 1) ? 1 : (1. / (dlg.m_nN - 1));
    double width = 1. + ((dlg.m_nN == 1) ? 0 : a);
    double s = dlg.m_dS0 * a;

    continuous_t barrier = make_barrier_fn(dlg.m_nN, dlg.m_dV0, a, s);

    double de = (dlg.m_dE2 - dlg.m_dE1);

    for (size_t i = 0; dlg.m_bWorking && (i < n_points); ++i)
    {
        double e      = dlg.m_dE1 + i * de / n_points;
        double k      = dlg.m_dL * std::sqrt(e);
        double period = 2 * M_PI / k;

        dfunc3_t < cv3 > alpha_beta = make_sweep_method_dfunc(barrier, e, dlg.m_dL);

        /* use 3-sigma-like rule to maximally reduce the interval
           is it applicable in our case? */
        double left_x  = - 6 * s;
        double right_x = (dlg.m_nN == 1) ? 6 * s : (1. + 6 * s);

        /* barrier has a number of wide gaps
           however we use constant step even if
           it is not efficient just for simplicity */
        dresult3 < cv3 > ab_ = rk4_solve3i < cv3 >
        (
            alpha_beta,
            left_x,
            right_x,
            s / 10,
            { - _i * k, 2 * _i * k * exp(_i * k * left_x) }
        );

        cv3 u = ab_.x.at<1>() / (_i * k - ab_.x.at<0>());

        transmission_plot.data->emplace_back(e, norm(u));

        if ((i % (n_points / 100)) == 0)
        {
            dlg.m_cTransmission.RedrawBuffer();
            dlg.Invoke([&] () {
                dlg.m_cTransmission.SwapBuffers();
                dlg.m_cTransmission.RedrawWindow();
            });
        }
    }

    dlg.m_bWorking = FALSE;

    return 0;
}

CDeltaBarrierDlg::CDeltaBarrierDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(CDeltaBarrierDlg::IDD, pParent)
    , m_pWorkerThread(NULL)
    , m_bWorking(FALSE)
    , m_dL(1)
    , m_dV0(10)
    , m_nN(2)
    , m_dS0(0.05)
    , m_dE1(0)
    , m_dE2(500)
    , m_dE(40)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CDeltaBarrierDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PLOT1, m_cBarrier);
    DDX_Control(pDX, IDC_PLOT2, m_cWaveFunc);
    DDX_Control(pDX, IDC_PLOT3, m_cTransmission);
    DDX_Text(pDX, IDC_EDIT1, m_dL);
    DDX_Text(pDX, IDC_EDIT2, m_dV0);
    DDX_Text(pDX, IDC_EDIT3, m_nN);
    DDX_Text(pDX, IDC_EDIT4, m_dS0);
    DDX_Text(pDX, IDC_EDIT5, m_dE1);
    DDX_Text(pDX, IDC_EDIT6, m_dE2);
    DDX_Text(pDX, IDC_EDIT8, m_dE);
    DDX_Control(pDX, IDC_PLOT4, m_cWaveFuncRe);
    DDX_Control(pDX, IDC_PLOT5, m_cWaveFuncIm);
}

BEGIN_MESSAGE_MAP(CDeltaBarrierDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
    ON_MESSAGE(WM_INVOKE, &CDeltaBarrierDlg::OnInvoke)
    ON_BN_CLICKED(IDC_BUTTON1, &CDeltaBarrierDlg::OnBnClickedButton1)
    ON_BN_CLICKED(IDC_BUTTON2, &CDeltaBarrierDlg::OnBnClickedButton2)
    ON_BN_CLICKED(IDC_BUTTON3, &CDeltaBarrierDlg::OnBnClickedButton3)
END_MESSAGE_MAP()


// CDeltaBarrierDlg message handlers

BOOL CDeltaBarrierDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

    auto_viewport_params params;
    params.factors = { 0, 0, 0.1, 0.1 };
    auto barrier_avp      = min_max_auto_viewport < points_t > ::create();
    auto wavefunc_avp     = min_max_auto_viewport < points_t > ::create();
    barrier_avp->set_params(params);
    wavefunc_avp->set_params(params);

    barrier_plot
        .with_view()
        .with_view_line_pen(plot::palette::pen(RGB(255, 255, 255), 2))
        .with_data()
        .with_auto_viewport(barrier_avp);
    wavefunc_plot
        .with_view()
        .with_view_line_pen(plot::palette::pen(RGB(255, 255, 255), 2))
        .with_data()
        .with_auto_viewport(wavefunc_avp);
    wavefunc_re_plot
        .with_view()
        .with_view_line_pen(plot::palette::pen(RGB(255, 255, 255), 2))
        .with_data()
        .with_auto_viewport(wavefunc_avp);
    wavefunc_im_plot
        .with_view()
        .with_view_line_pen(plot::palette::pen(RGB(255, 255, 255), 2))
        .with_data()
        .with_auto_viewport(wavefunc_avp);
    transmission_plot
        .with_view()
        .with_view_line_pen(plot::palette::pen(RGB(255, 255, 255), 3))
        .with_data()
        .with_static_viewport({ 0, 0, 0, 1.2 });

    m_cBarrier.background = palette::brush();
    m_cWaveFunc.background = palette::brush();
    m_cWaveFuncRe.background = palette::brush();
    m_cWaveFuncIm.background = palette::brush();
    m_cTransmission.background = palette::brush();

    m_cTransmission.triple_buffered = true;

    SetupPlot(m_cBarrier, barrier_plot);
    SetupPlot(m_cWaveFunc, wavefunc_plot);
    SetupPlot(m_cWaveFuncRe, wavefunc_re_plot);
    SetupPlot(m_cWaveFuncIm, wavefunc_im_plot);
    SetupPlot(m_cTransmission, transmission_plot);

    OnBnClickedButton3();
    OnBnClickedButton1();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

template < typename _container_t >
void CDeltaBarrierDlg::SetupPlot(PlotStatic & targetPlot,
                                 plot::simple_list_plot < _container_t > & layer)
{
    targetPlot.plot_layer.with(
        viewporter::create(
            tick_drawable::create(
                layer.view,
                const_n_tick_factory<axe::x>::create(
                    make_simple_tick_formatter(2, 5),
                    0,
                    5
                ),
                const_n_tick_factory<axe::y>::create(
                    make_simple_tick_formatter(2, 5),
                    0,
                    5
                ),
                palette::pen(RGB(80, 80, 80)),
                RGB(200, 200, 200)
            ),
            make_viewport_mapper(layer.viewport_mapper)
        )
    );
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CDeltaBarrierDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CDeltaBarrierDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CDeltaBarrierDlg::StartSimulationThread()
{
    if (this->m_bWorking)
    {
        return;
    }
    this->m_bWorking = TRUE;
    this->m_pWorkerThread = AfxBeginThread(&SimulationThreadProc, this, 0, 0, CREATE_SUSPENDED);
    this->m_pWorkerThread->m_bAutoDelete = FALSE;
    ResumeThread(this->m_pWorkerThread->m_hThread);
}


void CDeltaBarrierDlg::StopSimulationThread()
{
    if (this->m_bWorking)
    {
        this->m_bWorking = FALSE;
        while (MsgWaitForMultipleObjects(
            1, &this->m_pWorkerThread->m_hThread, FALSE, INFINITE, QS_SENDMESSAGE) != WAIT_OBJECT_0)
        {
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        //this->m_pWorkerThread->Delete();
        delete this->m_pWorkerThread;
        this->m_pWorkerThread = NULL;
    }
}


void CDeltaBarrierDlg::Invoke(const std::function < void () > & fn)
{
    SendMessage(WM_INVOKE, 0, (LPARAM)&fn);
}


afx_msg LRESULT CDeltaBarrierDlg::OnInvoke(WPARAM wParam, LPARAM lParam)
{
    (*(const std::function < void () > *) lParam)();
    return 0;
}

BOOL CDeltaBarrierDlg::DestroyWindow()
{
    StopSimulationThread();

    return CDialogEx::DestroyWindow();
}


void CDeltaBarrierDlg::OnBnClickedButton1()
{
    UpdateData(TRUE);

    StartSimulationThread();
}


void CDeltaBarrierDlg::OnBnClickedButton2()
{
    StopSimulationThread();
}


void CDeltaBarrierDlg::DrawBarrier()
{
    barrier_plot.data->resize(n_points);

    double a = (m_nN == 1) ? 1 : (1. / (m_nN - 1));
    double width = 1. + ((m_nN == 1) ? 0 : a);
    double s = m_dS0 * 1.;

    continuous_t barrier = make_barrier_fn(m_nN, m_dV0, a, s);

    for (size_t i = 0; i < n_points; ++i)
    {
        double x = - a / 2 + (double) i / n_points * width;
        barrier_plot.data->at(i) = { x, barrier(x) };
    }

    barrier_plot.refresh();

    m_cBarrier.RedrawWindow();
}


void CDeltaBarrierDlg::OnBnClickedButton3()
{
    UpdateData(TRUE);

    DrawBarrier();

    wavefunc_plot.data->resize(n_points);
    wavefunc_re_plot.data->resize(n_points);
    wavefunc_im_plot.data->resize(n_points);

    double a = (m_nN == 1) ? 1 : (1. / (m_nN - 1));
    double width = 1. + ((m_nN == 1) ? 0 : a);
    double s = m_dS0 * a;

    continuous_t barrier = make_barrier_fn(m_nN, m_dV0, a, s);

    double k      = m_dL * std::sqrt(m_dE);
    double period = 2 * M_PI / k;

    dfunc3_t < cv3 > alpha_beta = make_sweep_method_dfunc(barrier, m_dE, m_dL);

    /* use 3-sigma-like rule to maximally reduce the interval
       is it applicable in our case? */
    double left_x  = - 6 * s;
    double right_x = (m_nN == 1) ? 6 * s : (1. + 6 * s);

    /* barrier has a number of wide gaps
       however we use constant step even if
       it is not efficient just for simplicity */
    dresult3 < cv3 > ab_ = rk4_solve3i < cv3 >
    (
        alpha_beta,
        left_x,
        right_x,
        s / 10,
        { - _i * k, 2 * _i * k * exp(_i * k * left_x) }
    );

    cv3 u  = ab_.x.at<1>() / (_i * k - ab_.x.at<0>());
    cv3 du = _i * k * ab_.x.at<1>() / (_i * k - ab_.x.at<0>());

    dfunc3s_t < cv3 > wavefunc_dfunc = make_schrodinger_dfunc(barrier, m_dE, m_dL);

    dresult3s < cv3 > wavefunc = { right_x, u, du };

    double step = (double) 1. / (n_points / 2) * (period * 5 + 1.0);

    for (size_t i = 0; i < n_points / 2; ++i)
    {
        double x = wavefunc.t;
        wavefunc = rk4_solve3s < cv3 > (wavefunc_dfunc, x, -step, wavefunc.x, wavefunc.dx);
        wavefunc_plot.data->at(n_points / 2 - 1 - i) = { x, norm(wavefunc.x.at<0>()) };
        wavefunc_re_plot.data->at(n_points / 2 - 1 - i) = { x, wavefunc.x.at<0>().re };
        wavefunc_im_plot.data->at(n_points / 2 - 1 - i) = { x, wavefunc.x.at<0>().im };
    }

    wavefunc = { right_x, u, du };

    step = (double) 1. / (n_points / 2) * (period * 5);

    for (size_t i = 0; i < n_points / 2 + 1; ++i)
    {
        double x = wavefunc.t;
        wavefunc = rk4_solve3s < cv3 > (wavefunc_dfunc, x, step, wavefunc.x, wavefunc.dx);
        wavefunc_plot.data->at(n_points / 2 - 1 + i) = { x, norm(wavefunc.x.at<0>()) };
        wavefunc_re_plot.data->at(n_points / 2 - 1 + i) = { x, wavefunc.x.at<0>().re };
        wavefunc_im_plot.data->at(n_points / 2 - 1 + i) = { x, wavefunc.x.at<0>().im };
    }

    wavefunc_plot.auto_world->clear();
    wavefunc_plot.auto_world->adjust(*wavefunc_plot.data);
    wavefunc_plot.auto_world->adjust(*wavefunc_re_plot.data);
    wavefunc_plot.auto_world->adjust(*wavefunc_im_plot.data);
    wavefunc_plot.auto_world->flush();

    m_cWaveFunc.RedrawWindow();
    m_cWaveFuncRe.RedrawWindow();
    m_cWaveFuncIm.RedrawWindow();
}
