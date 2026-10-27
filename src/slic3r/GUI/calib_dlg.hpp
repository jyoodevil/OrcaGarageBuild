#ifndef slic3r_calib_dlg_hpp_
#define slic3r_calib_dlg_hpp_

#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/LabeledStaticBox.hpp"
#include "Widgets/RadioGroup.hpp"
#include "GUI_App.hpp"
#include "wx/hyperlink.h"
#include <wx/anybutton.h>
#include <wx/checklst.h>
#include <wx/event.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include "libslic3r/calib.hpp"
#include "libslic3r/GCode/OozeStop.hpp"

namespace Slic3r { namespace GUI {

class PA_Calibration_Dlg : public DPIDialog
{
public:
    PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~PA_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;
	void on_show(wxShowEvent& event);
protected:
    void reset_params();
	virtual void on_start(wxCommandEvent& event);
	virtual void on_extruder_type_changed(wxCommandEvent& event);
	virtual void on_method_changed(wxCommandEvent& event);

protected:
	bool m_bDDE;
	Calib_Params m_params;


	RadioGroup* m_rbExtruderType;
	RadioGroup* m_rbMethod;
	TextInput* m_tiStartPA;
	TextInput* m_tiEndPA;
	TextInput* m_tiPAStep;
	CheckBox* m_cbPrintNum;
	TextInput* m_tiBMAccels;
	TextInput* m_tiBMSpeeds;

	Plater* m_plater;
};

class Temp_Calibration_Dlg : public DPIDialog
{
public:
    Temp_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Temp_Calibration_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    
    virtual void on_start(wxCommandEvent& event);
    virtual void on_filament_type_changed(wxCommandEvent& event);
    void on_show(wxShowEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbFilamentType;
    RadioGroup* m_rbModel = nullptr;
    wxStaticBoxSizer* m_model_box = nullptr;
    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    CheckBox*  m_cbResize;
    Plater* m_plater;
};

class MaxVolumetricSpeed_Test_Dlg : public DPIDialog
{
public:
    MaxVolumetricSpeed_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~MaxVolumetricSpeed_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:

    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    Plater* m_plater;
};

class VFA_Test_Dlg : public DPIDialog {
public:
    VFA_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~VFA_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    CheckBox*  m_cbAutoAdjust;
    CheckBox*  m_cbResize;
    Plater* m_plater;
};


class Retraction_Test_Dlg : public DPIDialog
{
public:
    Retraction_Test_Dlg (wxWindow* parent, wxWindowID id, Plater* plater);
    ~Retraction_Test_Dlg ();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:

    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    TextInput* m_tiStart;
    TextInput* m_tiEnd;
    TextInput* m_tiStep;
    Plater* m_plater;
};

// OrcaGarageBuild: ooze ladder, reprime check and boost ladder (see libslic3r/GCode/OozeStop.hpp).
class Ooze_Reprime_Test_Dlg : public DPIDialog
{
public:
    Ooze_Reprime_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    void on_start(wxCommandEvent& event);
    void update_controls();

    Plater*          m_plater;
    OozeCalibParams  m_params;
    bool             m_has_chute{false};
    RadioGroup*      m_rbTest;
    RadioGroup*      m_rbStop;
    TextInput*       m_tiCool;
    TextInput*       m_tiExtraRetract;
    TextInput*       m_tiSettle;
    TextInput*       m_tiWaits;
    TextInput*       m_tiReprimeBase;
    TextInput*       m_tiReprimeStart;
    TextInput*       m_tiReprimeStep;
    TextInput*       m_tiReprimeCount;
    TextInput*       m_tiLadderLayers;
    TextInput*       m_tiWait;
    TextInput*       m_tiReprimeMid;
    TextInput*       m_tiCheckStep;
    TextInput*       m_tiBoxes;
    TextInput*       m_tiCheckLayers;
    TextInput*       m_tiBoostWaits;
    TextInput*       m_tiOozeMax;
    TextInput*       m_tiHalfLife;
    TextInput*       m_tiBoostStart;
    TextInput*       m_tiBoostStep;
    TextInput*       m_tiBoostCount;
    TextInput*       m_tiBoostDistance;
    TextInput*       m_tiBoostLayers;
    CheckBox*        m_cbPurge;
    wxStaticText*    m_stChuteNote;
    LabeledStaticBox* m_ladder_box;
    LabeledStaticBox* m_check_box;
    LabeledStaticBox* m_boost_box;
};

class Input_Shaping_Freq_Test_Dlg : public DPIDialog
{
public:
    Input_Shaping_Freq_Test_Dlg (wxWindow* parent, wxWindowID id, Plater* plater);
    ~Input_Shaping_Freq_Test_Dlg ();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
protected:

    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    RadioGroup* m_rbType;
    TextInput* m_tiFreqStartX;
    TextInput* m_tiFreqEndX;
    TextInput* m_tiFreqStartY;
    TextInput* m_tiFreqEndY;
    TextInput* m_tiDampingFactor;
    Plater* m_plater;
};

class Input_Shaping_Damp_Test_Dlg : public DPIDialog
{
public:
    Input_Shaping_Damp_Test_Dlg (wxWindow* parent, wxWindowID id, Plater* plater);
    ~Input_Shaping_Damp_Test_Dlg ();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
protected:

    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    RadioGroup* m_rbType;
    TextInput* m_tiFreqX;
    TextInput* m_tiFreqY;
    TextInput* m_tiDampingFactorStart;
    TextInput* m_tiDampingFactorEnd;
    Plater* m_plater;
};

class Cornering_Test_Dlg : public DPIDialog
{
public:
    Cornering_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater);
    ~Cornering_Test_Dlg();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
protected:
    virtual void on_start(wxCommandEvent& event);
    Calib_Params m_params;

    RadioGroup* m_rbModel;
    TextInput* m_tiJDStart;
    TextInput* m_tiJDEnd;
    Plater* m_plater;
};

class FlowRateCalibrationDialog : public DPIDialog
{
public:
    FlowRateCalibrationDialog(wxWindow* parent, wxWindowID id, Plater* plater);
    ~FlowRateCalibrationDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;

protected:
    virtual void on_start(wxCommandEvent& event);

    RadioGroup* m_rbType;
    // ORCA: use standard OrcaSlicer ComboBox instead of BitmapComboBox
    ComboBox* m_rbPattern;
    Plater* m_plater;
};
}} // namespace Slic3r::GUI
#endif
