#include "stdafx.h"

#include "MainDialog.h"

#include "MainApp.h"

static std::vector<byte> load_rcdata_as_vector(int name) {
	const auto resource = load_resource(RT_RCDATA, name);
	std::vector<byte> data(resource.begin(), resource.end());
	return data;
}

BEGIN_MESSAGE_MAP(MainDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_C_DUR_TONELADDER, &MainDialog::OnBnClickedCDurToneladder)
	ON_BN_CLICKED(IDC_C_DUR_TRIAD, &MainDialog::OnBnClickedCDurTriad)
	ON_BN_CLICKED(IDC_PCM_SOUND, &MainDialog::OnBnClickedPcmSound)
	ON_BN_CLICKED(IDC_TOGGLE_GUITAR, &MainDialog::OnBnClickedToggleGuitar)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_PIANO_264, IDC_PIANO_528, &MainDialog::OnBnClickedPiano)
END_MESSAGE_MAP()

MainDialog::MainDialog(CWnd* pParent) : CDialog(IDD_HTWAVGP_DIALOG, pParent) {
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void MainDialog::DoDataExchange(CDataExchange* pDX) {
	CDialog::DoDataExchange(pDX);
}

BOOL MainDialog::OnInitDialog() {
	CDialog::OnInitDialog();

	// Set the icon for this dialog. The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE); // Set big icon
	SetIcon(m_hIcon, FALSE); // Set small icon

	auto set_scrollbar_values = [this](int id, int min, int max, int pos) {
		auto slider = static_cast<CSliderCtrl*>(GetDlgItem(id));
		slider->SetRangeMin(min);
		slider->SetRangeMax(max);
		slider->SetTicFreq(1);
		slider->SetPos(pos);
	};
	set_scrollbar_values(IDC_VOLUME_SLIDER, DSBVOLUME_MIN, DSBVOLUME_MAX, DSBVOLUME_MAX);
	set_scrollbar_values(IDC_PAN_SLIDER, DSBPAN_LEFT, DSBPAN_RIGHT, 0);

	ds = direct_sound::context(m_hWnd);

	return TRUE; // return TRUE unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
// to draw the icon. For MFC applications using the document/view model,
// this is automatically done for you by the framework.
void MainDialog::OnPaint() {
	if (!IsIconic()) {
		CDialog::OnPaint();
		return;
	}

	CPaintDC dc(this); // device context for painting

	SendMessageW(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.m_hDC), 0);

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

// The system calls this function to obtain the cursor to
// display while the user drags the minimized window.
HCURSOR MainDialog::OnQueryDragIcon() {
	return static_cast<HCURSOR>(m_hIcon);
}

void MainDialog::OnHScroll(UINT code, UINT pos, CScrollBar* scrollBar) {
	UNREFERENCED_PARAMETER(pos);

	if (code != SB_THUMBTRACK) {
		return;
	}

	auto id = scrollBar->GetDlgCtrlID();

	if (id != IDC_VOLUME_SLIDER && id != IDC_PAN_SLIDER) {
		return;
	}

	auto value = reinterpret_cast<CSliderCtrl*>(scrollBar)->GetPos();
	auto set_volume_pan = [id, value](std::unique_ptr<direct_sound::playable>& buffer) {
		if (!buffer) {
			return;
		}

		switch (id) {
		case IDC_VOLUME_SLIDER:
			buffer->set_volume(value);
			break;
		case IDC_PAN_SLIDER:
			buffer->set_pan(value);
			break;
		}
	};

	set_volume_pan(c_dur_toneladder_buffer);

	for (auto& buffer : piano_buffers) {
		set_volume_pan(buffer);
	}
}

void MainDialog::OnBnClickedCDurToneladder() {
	auto button = static_cast<CButton*>(GetDlgItem(IDC_C_DUR_TONELADDER));
	auto isChecked = (button->GetState() & BST_CHECKED) == BST_CHECKED;

	if (!isChecked) {
		c_dur_toneladder_buffer.reset();
		return;
	}

	if (use_guitar_sound) {
		std::vector<std::vector<byte>> pcms;
		for (auto rc : guitar_c_dur_toneladder) {
			pcms.emplace_back(load_rcdata_as_vector(rc));
		}

		c_dur_toneladder_buffer = std::make_unique<direct_sound::double_buffer<int16_t, 2>>(
			ds,
			22050,
			22050,
			direct_sound::create_pcm_series_provider<int16_t, 2>(pcms)
		);
	} else {
		std::vector<size_t> toneladder(c_dur_toneladder.begin(), c_dur_toneladder.end());
		c_dur_toneladder_buffer = std::make_unique<direct_sound::double_buffer<int16_t, 2>>(
			ds,
			44100,
			44100 / 4,
			direct_sound::create_sine_wave_toneladder_provider<int16_t, 2>(toneladder)
		);
	}

	c_dur_toneladder_buffer->play(true);
}

void MainDialog::OnBnClickedCDurTriad() {
	auto button = static_cast<CButton*>(GetDlgItem(IDC_C_DUR_TRIAD));
	auto isChecked = (button->GetState() & BST_CHECKED) == BST_CHECKED;

	if (!isChecked) {
		for (auto& buffer : c_dur_triad_buffer) {
			buffer.reset();
		}
		return;
	}

	for (size_t i = 0; i < c_dur_triad_buffer.size(); ++i) {
		if (use_guitar_sound) {
			const auto pcm = load_rcdata_as_vector(guitar_c_dur_toneladder[i * 2]);
			c_dur_triad_buffer[i] = std::make_unique<direct_sound::double_buffer<int16_t, 2>>(
				ds,
				22050,
				22050,
				direct_sound::create_pcm_provider<int16_t, 2>(pcm, true)
			);
		} else {
			auto tone = c_dur_toneladder[i * 2];
			c_dur_triad_buffer[i] = std::make_unique<direct_sound::single_buffer<int16_t, 2>>(
				ds,
				44100,
				44100,
				direct_sound::create_sine_wave_provider<int16_t, 2>(tone)
			);
		}
	}

	for (auto& buffer : c_dur_triad_buffer) {
		buffer->play(true);
	}
}

void MainDialog::OnBnClickedPcmSound() {
	auto button = static_cast<CButton*>(GetDlgItem(IDC_PCM_SOUND));
	auto isChecked = (button->GetState() & BST_CHECKED) == BST_CHECKED;

	if (!isChecked) {
		pcm_buffer.reset();
		return;
	}

	const auto pcm = load_rcdata_as_vector(IDR_SAMPLE_SOUND);
	pcm_buffer = std::make_unique<direct_sound::double_buffer<int16_t, 2>>(
		ds,
		22050,
		22050,
		direct_sound::create_pcm_provider<int16_t, 2>(pcm, true)
	);
	pcm_buffer->play(true);
}

void MainDialog::OnBnClickedToggleGuitar() {
	auto button = static_cast<CButton*>(GetDlgItem(IDC_TOGGLE_GUITAR));
	use_guitar_sound = (button->GetState() & BST_CHECKED) == BST_CHECKED;
}

void MainDialog::OnBnClickedPiano(UINT sender) {
	auto button = static_cast<CButton*>(GetDlgItem(sender));
	auto isChecked = (button->GetState() & BST_CHECKED) == BST_CHECKED;
	auto index = sender - IDC_PIANO_264;

	if (!isChecked) {
		piano_buffers[index].reset();
		return;
	}

	if (use_guitar_sound) {
		const auto pcm = load_rcdata_as_vector(guitar_c_dur_toneladder[index]);
		piano_buffers[index] = std::make_unique<direct_sound::double_buffer<int16_t, 2>>(
			ds,
			22050,
			22050,
			direct_sound::create_pcm_provider<int16_t, 2>(pcm, true)
		);
	} else {
		auto tone = c_dur_toneladder[index];
		piano_buffers[index] = std::make_unique<direct_sound::single_buffer<int16_t, 2>>(
			ds,
			44100,
			44100,
			direct_sound::create_sine_wave_provider<int16_t, 2>(tone)
		);
	}

	piano_buffers[index]->play(true);
}
