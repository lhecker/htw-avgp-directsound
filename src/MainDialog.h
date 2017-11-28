#pragma once

#include "direct_sound.h"
#include "resource.h"

class MainDialog : public CDialog {
public:
	MainDialog(CWnd* pParent = nullptr);

private:
	static constexpr std::array<size_t, 8> c_dur_toneladder = {{
		264, // c
		297, // d
		330, // e
		352, // f
		396, // g
		440, // a
		495, // h
		528, // c
	}};
	static constexpr std::array<int, 8> guitar_c_dur_toneladder = {{
		IDR_GUITAR_264,
		IDR_GUITAR_297,
		IDR_GUITAR_330,
		IDR_GUITAR_352,
		IDR_GUITAR_396,
		IDR_GUITAR_440,
		IDR_GUITAR_495,
		IDR_GUITAR_528,
	}};

	HICON m_hIcon;
	direct_sound::context ds;
	std::unique_ptr<direct_sound::playable> c_dur_toneladder_buffer;
	std::array<std::unique_ptr<direct_sound::playable>, 3> c_dur_triad_buffer;
	std::unique_ptr<direct_sound::playable> pcm_buffer;
	std::array<std::unique_ptr<direct_sound::playable>, 9> piano_buffers;
	bool use_guitar_sound = false;

protected:
	virtual void DoDataExchange(CDataExchange* pDX) override;
	virtual BOOL OnInitDialog() override;

	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnHScroll(UINT code, UINT pos, CScrollBar* scrollBar);
	afx_msg void OnBnClickedCDurToneladder();
	afx_msg void OnBnClickedCDurTriad();
	afx_msg void OnBnClickedPcmSound();
	afx_msg void OnBnClickedToggleGuitar();
	afx_msg void OnBnClickedPiano(UINT sender);

	DECLARE_MESSAGE_MAP()
};
