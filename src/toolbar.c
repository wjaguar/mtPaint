/*	toolbar.c
	Copyright (C) 2006-2007 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "inifile.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "layer.h"
#include "viewer.h"
#include "channels.h"
#include "csel.h"


#include "graphics/xpm_paint.xpm"

#include "graphics/xbm_square.xbm"
#include "graphics/xbm_square_mask.xbm"

#include "graphics/xbm_circle.xbm"
#include "graphics/xbm_circle_mask.xbm"

#include "graphics/xbm_horizontal.xbm"
#include "graphics/xbm_horizontal_mask.xbm"

#include "graphics/xbm_vertical.xbm"
#include "graphics/xbm_vertical_mask.xbm"

#include "graphics/xbm_slash.xbm"
#include "graphics/xbm_slash_mask.xbm"

#include "graphics/xbm_backslash.xbm"
#include "graphics/xbm_backslash_mask.xbm"

#include "graphics/xbm_spray.xbm"
#include "graphics/xbm_spray_mask.xbm"

#include "graphics/xpm_shuffle.xpm"
#include "graphics/xbm_shuffle.xbm"
#include "graphics/xbm_shuffle_mask.xbm"

#include "graphics/xpm_flood.xpm"
#include "graphics/xbm_flood.xbm"
#include "graphics/xbm_flood_mask.xbm"

#include "graphics/xpm_line.xpm"
#include "graphics/xbm_line.xbm"
#include "graphics/xbm_line_mask.xbm"

#include "graphics/xpm_select.xpm"
#include "graphics/xbm_select.xbm"
#include "graphics/xbm_select_mask.xbm"

#include "graphics/xpm_smudge.xpm"
#include "graphics/xbm_smudge.xbm"
#include "graphics/xbm_smudge_mask.xbm"

#include "graphics/xpm_polygon.xpm"
#include "graphics/xbm_polygon.xbm"
#include "graphics/xbm_polygon_mask.xbm"

#include "graphics/xpm_clone.xpm"
#include "graphics/xbm_clone.xbm"
#include "graphics/xbm_clone_mask.xbm"

#include "graphics/xbm_move.xbm"
#include "graphics/xbm_move_mask.xbm"

#include "graphics/xpm_brcosa.xpm"
#include "graphics/xpm_flip_vs.xpm"
#include "graphics/xpm_flip_hs.xpm"
#include "graphics/xpm_rotate_cs.xpm"
#include "graphics/xpm_rotate_as.xpm"
#include "graphics/xpm_text.xpm"
#include "graphics/xpm_lasso.xpm"

#include "graphics/xpm_ellipse.xpm"
#include "graphics/xpm_ellipse2.xpm"
#include "graphics/xpm_rect1.xpm"
#include "graphics/xpm_rect2.xpm"
#include "graphics/xpm_pan.xpm"

#include "graphics/xpm_new.xpm"
#include "graphics/xpm_open.xpm"
#include "graphics/xpm_save.xpm"
#include "graphics/xpm_cut.xpm"
#include "graphics/xpm_copy.xpm"
#include "graphics/xpm_paste.xpm"
#include "graphics/xpm_undo.xpm"
#include "graphics/xpm_redo.xpm"

#include "graphics/xpm_up.xpm"
#include "graphics/xpm_down.xpm"
#include "graphics/xpm_centre.xpm"
#include "graphics/xpm_close.xpm"

#include "graphics/xpm_mode_cont.xpm"
#include "graphics/xpm_mode_opac.xpm"
#include "graphics/xpm_mode_tint.xpm"
#include "graphics/xpm_mode_tint2.xpm"
#include "graphics/xpm_mode_csel.xpm"
#include "graphics/xpm_mode_filt.xpm"
#include "graphics/xpm_mode_mask.xpm"

#include "graphics/xpm_grad_place.xpm"
#include "graphics/xbm_grad.xbm"
#include "graphics/xbm_grad_mask.xbm"


GtkWidget *icon_buttons[TOTAL_ICONS_TOOLS];

gboolean toolbar_status[TOOLBAR_MAX];			// True=show
GtkWidget *toolbar_boxes[TOOLBAR_MAX],			// Used for showing/hiding
	*drawing_col_prev;

GdkCursor *move_cursor;
GdkCursor *m_cursor[32];		// My mouse cursors



static GtkWidget *toolbar_zoom_main, *toolbar_zoom_view,
	*toolbar_labels[2],		// Colour A & B details
	*ts_spinslides[3],		// Size, flow, opacity
	*tb_label_opacity		// Opacity label
	;
static unsigned char mem_prev[PREVIEW_WIDTH * PREVIEW_HEIGHT * 3];
					// RGB colours, tool, pattern preview

typedef struct
{
	unsigned char ID;
	signed char radio;
	unsigned char sep, rclick;
	int actmap;
	char *tooltip, **xpm;
	GtkWidget *widget;
} toolbar_item;

static void fill_toolbar(GtkToolbar *bar, toolbar_item *items,
	GtkSignalFunc lclick, int lbase, GtkSignalFunc rclick, int rbase);

#undef _
#define _(X) X

static toolbar_item layer_bar[] = {
	{ LTB_NEW,    -1, 0, 0, 0, _("New Layer"), xpm_new_xpm },
	{ LTB_RAISE,  -1, 0, 0, 0, _("Raise"), xpm_up_xpm },
	{ LTB_LOWER,  -1, 0, 0, 0, _("Lower"), xpm_down_xpm },
	{ LTB_DUP,    -1, 0, 0, 0, _("Duplicate Layer"), xpm_copy_xpm },
	{ LTB_CENTER, -1, 0, 0, 0, _("Centralise Layer"), xpm_centre_xpm },
	{ LTB_DEL,    -1, 0, 0, 0, _("Delete Layer"), xpm_cut_xpm },
	{ LTB_CLOSE,  -1, 0, 0, 0, _("Close Layers Window"), xpm_close_xpm },
	{ 0, 0, 0, 0, 0, NULL, NULL }};

#undef _
#define _(X) __(X)

/* Create toolbar for layers window */
GtkWidget *layer_toolbar(GtkWidget **wlist)
{		
	int i;
	GtkWidget *toolbar;

#if GTK_MAJOR_VERSION == 1
	toolbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
#endif
	fill_toolbar(GTK_TOOLBAR(toolbar), layer_bar,
		GTK_SIGNAL_FUNC(layer_iconbar_click), 0, NULL, 0);
	gtk_widget_show(toolbar);

	for (i = 0; i < TOTAL_ICONS_LAYER; i++)
		wlist[i] = layer_bar[i].widget;

	return toolbar;
}

static gint expose_preview( GtkWidget *widget, GdkEventExpose *event )
{
	int rx, ry, rw, rh;

	rx = event->area.x;
	ry = event->area.y;
	rw = event->area.width;
	rh = event->area.height;

	if ( ry < PREVIEW_HEIGHT )
	{
		if ( (ry+rh) >= PREVIEW_HEIGHT )
		{
			rh = PREVIEW_HEIGHT - ry;
		}
		gdk_draw_rgb_image( widget->window, widget->style->black_gc,
				rx, ry, rw, rh,
				GDK_RGB_DITHER_NONE,
				mem_prev + 3*( rx + PREVIEW_WIDTH*ry ),
				PREVIEW_WIDTH*3
				);
	}

	return FALSE;
}


static gint click_colours( GtkWidget *widget, GdkEventButton *event )
{
	if (mem_img[CHN_IMAGE])
	{
		if ( event->y > 31 ) choose_pattern(0);
		else
		{
			if ( event->x < 48 ) choose_colours();
			else choose_pattern(1);
		}
	}

	return FALSE;
}

static GtkWidget *toolbar_add_zoom(GtkWidget *box)		// Add zoom combo box
{
	int i;
	static char *txt[] = { "10%", "20%", "25%", "33%", "50%", "100%",
		"200%", "300%", "400%", "800%", "1200%", "1600%", "2000%", NULL };
	GtkWidget *combo, *combo_entry;
	GList *combo_list = NULL;


	combo = pack(box, gtk_combo_new());
	gtk_combo_set_value_in_list (GTK_COMBO (combo), FALSE, FALSE);
	gtk_widget_show (combo);
	combo_entry = GTK_COMBO (combo)->entry;
	GTK_WIDGET_UNSET_FLAGS (combo_entry, GTK_CAN_FOCUS);
	gtk_widget_set_usize(GTK_COMBO(combo)->button, 18, -1);


#if GTK_MAJOR_VERSION == 1
	gtk_widget_set_usize(combo, 75, -1);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_entry_set_width_chars( GTK_ENTRY(combo_entry), 6);
#endif

	gtk_entry_set_editable( GTK_ENTRY(combo_entry), FALSE );

	for ( i=0; txt[i]; i++ ) combo_list = g_list_append( combo_list, txt[i] );

	gtk_combo_set_popdown_strings( GTK_COMBO(combo), combo_list );
	g_list_free( combo_list );
	gtk_entry_set_text( GTK_ENTRY(combo_entry), "100%" );

	return combo;
}

static float toolbar_get_zoom( GtkWidget *combo )
{
	int i = 0;
	float res = 0;
	char *txt = (char *) gtk_entry_get_text( GTK_ENTRY(GTK_COMBO (combo)->entry) );

	if ( strlen(txt) > 2)		// Weed out bogus calls
	{
		sscanf(txt, "%i%%", &i);
		res = (float)i / 100;
		if (res < 1.0) res = 1.0 / rint(1.0 / res);
	}

	return res;
}

void toolbar_viewzoom(gboolean visible)
{
	if ( visible )
		gtk_widget_show( toolbar_zoom_view );
	else
		gtk_widget_hide( toolbar_zoom_view );
}

void toolbar_zoom_update()			// Update the zoom combos to reflect current zoom
{
	char txt[32];

	if ( toolbar_zoom_main == NULL ) return;
	if ( toolbar_zoom_view == NULL ) return;

	snprintf( txt, 30, "%i%%", (int)(can_zoom*100) );
	gtk_entry_set_text( GTK_ENTRY(GTK_COMBO(toolbar_zoom_main)->entry), txt );

	snprintf( txt, 30, "%i%%", (int)(vw_zoom*100) );
	gtk_entry_set_text( GTK_ENTRY(GTK_COMBO(toolbar_zoom_view)->entry), txt );
}

static void toolbar_zoom_main_change()
{
	float new = toolbar_get_zoom( toolbar_zoom_main );

	if ( new > 0 ) align_size( new );
}

static void toolbar_zoom_view_change()
{
	float new = toolbar_get_zoom( toolbar_zoom_view );

	if ( new > 0 ) vw_align_size( new );
}

static int *vars_settings[TOTAL_SETTINGS] = {
	&mem_continuous, &mem_undo_opacity,
	&tint_mode[0], &tint_mode[1],
	&mem_cselect, &mem_filtmode,
	&mem_unmask, &mem_gradient
};

void toolbar_mode_change(GtkWidget *widget, gpointer data)
{
	gint j = (gint) data;

	*(vars_settings[j]) = !!GTK_TOGGLE_BUTTON(widget)->active;

	switch (j)
	{
	case SETB_CSEL:
		if (mem_cselect && !csel_data)
		{
			csel_init();
			mem_cselect = !!csel_data;
		}
		break;
	case SETB_GRAD:
		if ((gradient[mem_channel].status == GRAD_DONE) &&
			(tool_type != TOOL_GRADIENT))
			repaint_grad(mem_gradient);
		break;
	}
	gtk_widget_queue_draw(drawing_canvas);
}

static int set_flood(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin, *toggle;
	GList *chain = GTK_BOX(box)->children;

	spin = ((GtkBoxChild*)chain->data)->widget;
	flood_step = read_float_spin(spin);
	chain = chain->next;
	toggle = ((GtkBoxChild*)chain->data)->widget;
	flood_cube = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle));
	chain = chain->next;
	toggle = ((GtkBoxChild*)chain->data)->widget;
	flood_img = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle));
	chain = chain->next;
	toggle = ((GtkBoxChild*)chain->data)->widget;
	flood_slide = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle));

	return TRUE;
}

static int set_smudge(GtkWidget *box, gpointer fdata)
{
	GtkWidget *toggle;

	toggle = BOX_CHILD_0(box);
	smudge_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle));

	return TRUE;
}

static int set_brush_step(GtkWidget *box, gpointer fdata)
{
	GtkWidget *spin;

	spin = BOX_CHILD_0(box);
	brush_spacing = read_spin(spin);

	return TRUE;
}

static int set_filtmode(GtkWidget *box, gpointer fdata)
{
	GtkWidget *hbox;
	int i, j, bits = 0;

	hbox = BOX_CHILD_0(box);
	for (i = 0; ; i++)
	{
		for (j = 0; j < 3; j++)
		{
			if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(BOX_CHILD(hbox, j))))
				bits |= 1 << (i * 3 + j);
		}
		if (i) break;
		hbox = BOX_CHILD_2(box);
	}

	/* Don't accept pass-all or stop-all masks */
	if ((bits == 0x3F) || !(bits & 7) || !(bits & 0x38)) return (FALSE);

	filter_HSV = bits & 7;
	filter_RGB = bits >> 3;

	return (TRUE);
}

static gboolean toolbar_rclick(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	char *hsvrgb[6] = { _("Hue"), _("Saturation"), _("Value"),
		_("Red"), _("Green"), _("Blue") };
	GtkWidget *box, *hbox;
	int i, j, k;

	/* Handle only right clicks */
	if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3))
		return (FALSE);

	switch ((gint)user_data)
	{
	case SETB_CONT: /* Brush spacing */
		box = gtk_vbox_new(FALSE, 5);
		gtk_widget_show(box);
		pack(box, add_a_spin(brush_spacing, 0, MAX_WIDTH));
// !!! Not implemented yet
//		add_a_toggle(_("Flat gradient strokes"), box, ???);
		filter_window(_("Brush spacing"), box, set_brush_step, NULL, TRUE);
		break;
	case SETB_CSEL:
		colour_selector(COLSEL_EDIT_CSEL);
		break;
	case SETB_GRAD: /* Gradient selector */
		gradient_setup(1);
		break;
	case SETB_FILT: /* Component filter */
		box = gtk_vbox_new(FALSE, 5);
		k = filter_HSV;
		for (i = 0; ; i++)
		{
			hbox = pack(box, gtk_hbox_new(TRUE, 5));
			for (j = 0; j < 3; j++)
			{
				pack(hbox, sig_toggle(hsvrgb[i * 3 + j],
					k & (1 << j), NULL, NULL));
			}
			if (i) break;
			add_hseparator(box, -2, 10);
			k = filter_RGB;
		}
		gtk_widget_show_all(box);
		filter_window(_("Components to pass"), box, set_filtmode, NULL, TRUE);
		break;
	case (TTB_0 + TTB_FLOOD): /* Flood fill step */
		box = gtk_vbox_new(FALSE, 5);
		gtk_widget_show(box);
		pack(box, add_float_spin(flood_step, 0, 200));
		add_a_toggle(_("RGB Cube"), box, flood_cube);
		add_a_toggle(_("By image channel"), box, flood_img);
		add_a_toggle(_("Gradient-driven"), box, flood_slide);
		filter_window(_("Fill settings"), box, set_flood, NULL, TRUE);
		break;
	case (TTB_0 + TTB_SMUDGE): /* Smudge opacity mode */
		box = gtk_vbox_new(FALSE, 5);
		gtk_widget_show(box);
		add_a_toggle(_("Respect opacity mode"), box, smudge_mode);
		filter_window(_("Smudge settings"), box, set_smudge, NULL, TRUE);
		break;
	case (TTB_0 + TTB_GRAD): /* Gradient config */
		gradient_setup(0);
		break;
	default: /* For other buttons, do nothing */
		return (FALSE);
	}
	return (TRUE);
}


static void ts_update_spinslides()
{
	mt_spinslide_set_value(ts_spinslides[0], tool_size);
	mt_spinslide_set_value(ts_spinslides[1], tool_flow);
	mt_spinslide_set_value(ts_spinslides[2], mem_channel == CHN_IMAGE ?
		tool_opacity : channel_col_A[mem_channel]);
}


static void ts_spinslide_moved(GtkAdjustment *adj, gpointer user_data)
{
	int n, i;

	n = ADJ2INT(adj);
	switch ((int)user_data)
	{
	case 0: tool_size = n;
		break;
	case 1:	tool_flow = n;
		break;
	case 2:	i = mem_channel == CHN_IMAGE ? tool_opacity :
			channel_col_A[mem_channel];
		if (n != i) pressed_opacity(n);
		break;
	}
}


static void toolbar_settings_exit()
{
	toolbar_status[TOOLBAR_SETTINGS] = FALSE;
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(menu_widgets[MENU_TBSET]), FALSE);
	toolbar_exit();
}

static void fill_toolbar(GtkToolbar *bar, toolbar_item *items,
	GtkSignalFunc lclick, int lbase, GtkSignalFunc rclick, int rbase)
{
	GtkWidget *iconw, *radio[32];
	GdkPixmap *icon, *mask;

	memset(radio, 0, sizeof(radio));
	for (; items->xpm; items++)
	{
		icon = gdk_pixmap_create_from_xpm_d(main_window->window, &mask,
			NULL, items->xpm);
		iconw = gtk_pixmap_new(icon, mask);
		gdk_pixmap_unref(icon);
		gdk_pixmap_unref(mask);
		items->widget = gtk_toolbar_append_element(bar,
			items->radio < 0 ? GTK_TOOLBAR_CHILD_BUTTON :
			items->radio ? GTK_TOOLBAR_CHILD_RADIOBUTTON :
			GTK_TOOLBAR_CHILD_TOGGLEBUTTON,
			items->radio > 0 ? radio[items->radio] : NULL,
			NULL, _(items->tooltip), "Private", iconw, lclick,
			(gpointer)(items->ID + lbase));
		if (items->radio > 0) radio[items->radio] = items->widget;
		if (items->rclick) gtk_signal_connect(GTK_OBJECT(items->widget),
			"button_press_event", rclick, (gpointer)(items->ID + rbase));
		if (items->sep) gtk_toolbar_append_space(bar);
		mapped_dis_add(items->widget, items->actmap);
	}
}

#define GP_WIDTH 256
#define GP_HEIGHT 16
static GtkWidget *grad_view;

#undef _
#define _(X) X

static toolbar_item settings_bar[] = {
	{ SETB_CONT, 0, 0, 1, 0, _("Continuous Mode"), xpm_mode_cont_xpm },
	{ SETB_OPAC, 0, 0, 0, 0, _("Opacity Mode"), xpm_mode_opac_xpm },
	{ SETB_TINT, 0, 0, 0, 0, _("Tint Mode"), xpm_mode_tint_xpm },
	{ SETB_TSUB, 0, 0, 0, 0, _("Tint +-"), xpm_mode_tint2_xpm },
	{ SETB_CSEL, 0, 0, 1, 0, _("Colour-Selective Mode"), xpm_mode_csel_xpm },
	{ SETB_FILT, 0, 0, 1, 0, _("Filtered Mode"), xpm_mode_filt_xpm },
	{ SETB_MASK, 0, 0, 0, 0, _("Disable All Masks"), xpm_mode_mask_xpm },
	{ 0, 0, 0, 0, 0, NULL, NULL }};

#undef _
#define _(X) __(X)

static void toolbar_settings_init()
{
	int i, vals[] = {tool_size, tool_flow, tool_opacity};
	char *ts_titles[] = { _("Size"), _("Flow"), _("Opacity") };

	GtkWidget *label, *vbox, *table, *toolbar_settings, *labels[3];
	GtkWidget *button;
	GdkPixmap *pmap;


	if ( toolbar_boxes[TOOLBAR_SETTINGS] )
	{
		gtk_widget_show( toolbar_boxes[TOOLBAR_SETTINGS] );	// Used when Home key is pressed
#if GTK_MAJOR_VERSION == 1
		gtk_widget_queue_resize(toolbar_boxes[TOOLBAR_SETTINGS]); /* Re-render sliders */
#endif
		return;
	}

///	SETTINGS TOOLBAR

	toolbar_boxes[TOOLBAR_SETTINGS] = add_a_window(GTK_WINDOW_TOPLEVEL,
		_("Settings Toolbar"), GTK_WIN_POS_NONE, FALSE);

	gtk_widget_set_uposition( toolbar_boxes[TOOLBAR_SETTINGS],
		inifile_get_gint32("toolbar_settings_x", 0 ),
		inifile_get_gint32("toolbar_settings_y", 0 ) );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show(vbox);
	gtk_container_add (GTK_CONTAINER (toolbar_boxes[TOOLBAR_SETTINGS]), vbox);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

#if GTK_MAJOR_VERSION == 1
	toolbar_settings = pack(vbox, gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
		GTK_TOOLBAR_ICONS));
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar_settings = pack(vbox, gtk_toolbar_new());
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar_settings), GTK_TOOLBAR_ICONS);
#endif

	fill_toolbar(GTK_TOOLBAR(toolbar_settings), settings_bar,
		GTK_SIGNAL_FUNC(toolbar_mode_change), 0,
		GTK_SIGNAL_FUNC(toolbar_rclick), 0);

	for (i = 0; i < TOTAL_ICONS_SETTINGS; i++)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(settings_bar[i].widget),
			!!*(vars_settings[i]));
	}

	gtk_widget_show(toolbar_settings);

	/* Gradient mode button+preview */
	button = pack(vbox, gtk_toggle_button_new());
	pmap = gdk_pixmap_new(main_window->window, GP_WIDTH, GP_HEIGHT, -1);
	grad_view = gtk_pixmap_new(pmap, NULL);
	gdk_pixmap_unref(pmap);
	gtk_pixmap_set_build_insensitive(GTK_PIXMAP(grad_view), FALSE);
	gtk_container_add(GTK_CONTAINER(button), grad_view);
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 4) /* GTK+ 2.4+ */
	gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
#endif
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), !!mem_gradient);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(toolbar_mode_change), (gpointer)SETB_GRAD);
	gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
		GTK_SIGNAL_FUNC(toolbar_rclick), (gpointer)SETB_GRAD);
	gtk_widget_show_all(button);
	gtk_widget_realize(grad_view);
	/* Parasite gradient tooltip on settings toolbar */
	gtk_tooltips_set_tip(GTK_TOOLBAR(toolbar_settings)->tooltips,
		button, _("Gradient Mode"), "Private");

	/* Colors A & B */
	label = pack(vbox, gtk_label_new(""));
	toolbar_labels[0] = label;
	gtk_misc_set_alignment( GTK_MISC(label), 0, 0.5 );
	gtk_widget_show (label);
	gtk_misc_set_padding (GTK_MISC (label), 5, 2);

	label = pack(vbox, gtk_label_new(""));
	toolbar_labels[1] = label;
	gtk_misc_set_alignment( GTK_MISC(label), 0, 0.5 );
	gtk_widget_show (label);
	gtk_misc_set_padding (GTK_MISC (label), 5, 2);

	if (mem_channel != CHN_IMAGE) vals[2] = channel_col_A[mem_channel];
	table = add_a_table(3, 2, 5, vbox);
	for (i = 0; i < 3; i++)
	{
		labels[i] = add_to_table(ts_titles[i], table, i, 0, 0);
		ts_spinslides[i] = mt_spinslide_new(-1, -1);
		gtk_table_attach(GTK_TABLE(table), ts_spinslides[i], 1, 2,
			i, i + 1, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
		mt_spinslide_set_range(ts_spinslides[i], i == 2 ? 0 : 1, 255);
		mt_spinslide_connect(ts_spinslides[i],
			GTK_SIGNAL_FUNC(ts_spinslide_moved), (gpointer)i);
	}
	tb_label_opacity = labels[2];

	gtk_signal_connect(GTK_OBJECT(toolbar_boxes[TOOLBAR_SETTINGS]),
		"delete_event", GTK_SIGNAL_FUNC(toolbar_settings_exit), NULL);
	gtk_window_set_transient_for( GTK_WINDOW(toolbar_boxes[TOOLBAR_SETTINGS]),
		GTK_WINDOW(main_window) );

	toolbar_update_settings();

	gtk_widget_show( toolbar_boxes[TOOLBAR_SETTINGS] );
}

#undef _
#define _(X) X

static toolbar_item main_bar[] = {
	{ MTB_NEW, -1, 0, 0, 0, _("New Image"), xpm_new_xpm },
	{ MTB_OPEN, -1, 0, 0, 0, _("Load Image File"), xpm_open_xpm },
	{ MTB_SAVE, -1, 1, 0, 0, _("Save Image File"), xpm_save_xpm },
	{ MTB_CUT, -1, 0, 0, NEED_SEL2, _("Cut"), xpm_cut_xpm },
	{ MTB_COPY, -1, 0, 0, NEED_SEL2, _("Copy"), xpm_copy_xpm },
	{ MTB_PASTE, -1, 1, 0, NEED_CLIP, _("Paste"), xpm_paste_xpm },
	{ MTB_UNDO, -1, 0, 0, NEED_UNDO, _("Undo"), xpm_undo_xpm },
	{ MTB_REDO, -1, 1, 0, NEED_REDO, _("Redo"), xpm_redo_xpm },
	{ MTB_BRCOSA, -1, 0, 0, 0, _("Transform Colour"), xpm_brcosa_xpm },
	{ MTB_PAN, -1, 0, 0, 0, _("Pan Window"), xpm_pan_xpm },
	{ 0, 0, 0, 0, 0, NULL, NULL }};

static toolbar_item tools_bar[] = {
	{ TTB_PAINT, 1, 0, 0, 0, _("Paint"), xpm_paint_xpm },
	{ TTB_SHUFFLE, 1, 0, 0, 0, _("Shuffle"), xpm_shuffle_xpm },
	{ TTB_FLOOD, 1, 0, 1, 0, _("Flood Fill"), xpm_flood_xpm },
	{ TTB_LINE, 1, 0, 0, 0, _("Straight Line"), xpm_line_xpm },
	{ TTB_SMUDGE, 1, 0, 1, NEED_24, _("Smudge"), xpm_smudge_xpm },
	{ TTB_CLONE, 1, 0, 0, 0, _("Clone"), xpm_clone_xpm },
	{ TTB_SELECT, 1, 0, 0, 0, _("Make Selection"), xpm_select_xpm },
	{ TTB_POLY, 1, 0, 0, 0, _("Polygon Selection"), xpm_polygon_xpm },
	{ TTB_GRAD, 1, 1, 1, 0, _("Place Gradient"), xpm_grad_place_xpm },
	{ TTB_LASSO, -1, 0, 0, NEED_LASSO, _("Lasso Selection"), xpm_lasso_xpm },
	{ TTB_TEXT, -1, 1, 0, 0, _("Paste Text"), xpm_text_xpm },
	{ TTB_ELLIPSE, -1, 0, 0, NEED_SEL, _("Ellipse Outline"), xpm_ellipse2_xpm },
	{ TTB_FELLIPSE, -1, 0, 0, NEED_SEL, _("Filled Ellipse"), xpm_ellipse_xpm },
	{ TTB_OUTLINE, -1, 0, 0, NEED_SEL2, _("Outline Selection"), xpm_rect1_xpm },
	{ TTB_FILL, -1, 1, 0, NEED_SEL2, _("Fill Selection"), xpm_rect2_xpm },
	{ TTB_SELFV, -1, 0, 0, NEED_CLIP, _("Flip Selection Vertically"), xpm_flip_vs_xpm },
	{ TTB_SELFH, -1, 0, 0, NEED_CLIP, _("Flip Selection Horizontally"), xpm_flip_hs_xpm },
	{ TTB_SELRCW, -1, 0, 0, NEED_CLIP, _("Rotate Selection Clockwise"), xpm_rotate_cs_xpm },
	{ TTB_SELRCCW, -1, 0, 0, NEED_CLIP, _("Rotate Selection Anti-Clockwise"), xpm_rotate_as_xpm },
	{ 0, 0, 0, 0, 0, NULL, NULL }};

#undef _
#define _(X) __(X)

void toolbar_init(GtkWidget *vbox_main)
{
	char txt[32];
	int i;

	GdkPixmap *icon, *mask;
	GtkWidget *toolbar_main, *toolbar_tools, *hbox;
	GdkColor cfg = { -1, -1, -1, -1 }, cbg = { 0, 0, 0, 0 };

	static char *xbm_list[TOTAL_CURSORS] = { xbm_square_bits, xbm_circle_bits,
		xbm_horizontal_bits, xbm_vertical_bits, xbm_slash_bits, xbm_backslash_bits,
		xbm_spray_bits, xbm_shuffle_bits, xbm_flood_bits, xbm_select_bits, xbm_line_bits,
		xbm_smudge_bits, xbm_polygon_bits, xbm_clone_bits, xbm_grad_bits
		},
	*xbm_mask_list[TOTAL_CURSORS] = { xbm_square_mask_bits, xbm_circle_mask_bits,
		xbm_horizontal_mask_bits, xbm_vertical_mask_bits, xbm_slash_mask_bits,
		xbm_backslash_mask_bits, xbm_spray_mask_bits, xbm_shuffle_mask_bits,
		xbm_flood_mask_bits, xbm_select_mask_bits, xbm_line_mask_bits,
		xbm_smudge_mask_bits, xbm_polygon_mask_bits, xbm_clone_mask_bits,
		xbm_grad_mask_bits
		};
	static unsigned char cursor_tip[TOTAL_CURSORS][2] = { {0, 0}, {0, 0},
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 19},
		{10, 10}, {0, 0}, {0, 0}, {10, 10}, {0, 0}, {0, 0} };

	for ( i=1; i<TOOLBAR_MAX; i++ )
	{
		sprintf(txt, "toolbar%i", i);
		toolbar_status[i] = inifile_get_gboolean( txt, TRUE );

		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
			menu_widgets[MENU_TBMAIN - TOOLBAR_MAIN + i]),
			toolbar_status[i]);	// Menu toggles = status
	}

///	MAIN TOOLBAR

	toolbar_boxes[TOOLBAR_MAIN] = hbox = pack(vbox_main, gtk_hbox_new(FALSE, 0));
	if (toolbar_status[TOOLBAR_MAIN]) gtk_widget_show(hbox); // Only show if user wants

#if GTK_MAJOR_VERSION == 1
	toolbar_main = pack(hbox, gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
		GTK_TOOLBAR_ICONS));
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar_main = pack(hbox, gtk_toolbar_new());
	gtk_toolbar_set_style( GTK_TOOLBAR(toolbar_main), GTK_TOOLBAR_ICONS );
#endif

	toolbar_zoom_main = toolbar_add_zoom(hbox);
	toolbar_zoom_view = toolbar_add_zoom(hbox);
	/* In GTK1, combo box entry is updated continuously */
#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_main)->popwin),
		"hide", GTK_SIGNAL_FUNC(toolbar_zoom_main_change), NULL);
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_view)->popwin),
		"hide", GTK_SIGNAL_FUNC(toolbar_zoom_view_change), NULL);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_main)->entry),
		"changed", GTK_SIGNAL_FUNC(toolbar_zoom_main_change), NULL);
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_view)->entry),
		"changed", GTK_SIGNAL_FUNC(toolbar_zoom_view_change), NULL);
#endif
	toolbar_viewzoom(FALSE);

	for (i=0; i<TOTAL_CURSORS; i++)
	{
		icon = gdk_bitmap_create_from_data (NULL, xbm_list[i], 20, 20);
		mask = gdk_bitmap_create_from_data (NULL, xbm_mask_list[i], 20, 20);

		m_cursor[i] = gdk_cursor_new_from_pixmap(icon, mask, &cfg, &cbg,
			cursor_tip[i][0], cursor_tip[i][1]);

		gdk_pixmap_unref( icon );
		gdk_pixmap_unref( mask );
	}
	icon = gdk_bitmap_create_from_data (NULL, xbm_move_bits, 20, 20);
	mask = gdk_bitmap_create_from_data (NULL, xbm_move_mask_bits, 20, 20);
	move_cursor = gdk_cursor_new_from_pixmap (icon, mask, &cfg, &cbg, 10, 10);
	gdk_pixmap_unref( icon );
	gdk_pixmap_unref( mask );

	fill_toolbar(GTK_TOOLBAR(toolbar_main), main_bar,
		GTK_SIGNAL_FUNC(toolbar_icon_event2), 0, NULL, 0);

	gtk_widget_show(toolbar_main);


///	TOOLS TOOLBAR

	toolbar_boxes[TOOLBAR_TOOLS] = hbox = pack(vbox_main, gtk_hbox_new(FALSE, 0));
	if (toolbar_status[TOOLBAR_TOOLS]) gtk_widget_show(hbox); // Only show if user wants

#if GTK_MAJOR_VERSION == 1
	toolbar_tools = pack(hbox, gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
		GTK_TOOLBAR_ICONS));
#endif
#if GTK_MAJOR_VERSION == 2
	toolbar_tools = pack(hbox, gtk_toolbar_new());
	gtk_toolbar_set_style( GTK_TOOLBAR(toolbar_tools), GTK_TOOLBAR_ICONS );
#endif

	fill_toolbar(GTK_TOOLBAR(toolbar_tools), tools_bar,
		GTK_SIGNAL_FUNC(toolbar_icon_event), 0,
		GTK_SIGNAL_FUNC(toolbar_rclick), TTB_0);
	for (i = 0; tools_bar[i].xpm; i++)
	{
		icon_buttons[tools_bar[i].ID] = tools_bar[i].widget;
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON]), TRUE);

	gtk_widget_show(toolbar_tools);
}

void ts_update_gradient()
{
	unsigned char rgb[GP_WIDTH * GP_HEIGHT * 3], cset[3];
	unsigned char pal[256 * 3], *tmp = NULL, *dest;
	int i, j, k, op, op2, frac, slot, idx = 255, wrk[NUM_CHANNELS + 3];
	GdkPixmap *pmap;

	if (!grad_view) return;

	slot = mem_channel + 1;
	if (mem_channel != CHN_IMAGE) /* Create pseudo palette */
	{
		for (j = 0; j < 3; j++)
		for (i = 0; i < 256; i++)
		{
			k = mem_background * 255 + (channel_rgb[mem_channel][j] -
				mem_background) * i + 127;
			pal[i * 3 + j] = (k + (k >> 8) + 1) >> 8;
		}
	}
	else if (mem_img_bpp == 1) /* Read in current palette */
	{
		memset(pal, 0, sizeof(pal));
		for (i = 0; i < 256; i++)
		{
			pal[i * 3 + 0] = mem_pal[i].red;
			pal[i * 3 + 1] = mem_pal[i].green;
			pal[i * 3 + 2] = mem_pal[i].blue;
		}
	}
	else tmp = cset , --slot; /* Use gradient colors */
	if ((mem_img_bpp == 3) && (mem_channel <= CHN_ALPHA))
		idx = 0; /* Allow intermediate opacities */

	/* Draw the preview, ignoring RGBA coupling */
	memset(rgb, mem_background, sizeof(rgb));
	for (i = 0; i < GP_WIDTH; i++)
	{
		dest = rgb + i * 3;
		wrk[CHN_IMAGE + 3] = 0;
		op = grad_value(wrk, slot, i * (1.0 / (double)(GP_WIDTH - 1)));
		if (!op) continue;
		for (j = 0; j < GP_HEIGHT; j++ , dest += GP_WIDTH * 3)
		{
			frac = BAYER(i, j);
			op2 = (op + frac) >> 8;
			if (!op2) continue;
			op2 |= idx;
			if (tmp == cset)
			{
				cset[0] = (wrk[0] + frac) >> 8;
				cset[1] = (wrk[1] + frac) >> 8;
				cset[2] = (wrk[2] + frac) >> 8;
			}
			else
			{
				k = (wrk[mem_channel + 3] + frac) >> 8;
				if (mem_channel == CHN_IMAGE) k = wrk[k];
				tmp = pal + 3 * k;
			}
			k = dest[0] * 255 + (tmp[0] - dest[0]) * op2;
			dest[0] = (k + (k >> 8) + 1) >> 8;
			k = dest[1] * 255 + (tmp[1] - dest[1]) * op2;
			dest[1] = (k + (k >> 8) + 1) >> 8;
			k = dest[2] * 255 + (tmp[2] - dest[2]) * op2;
			dest[2] = (k + (k >> 8) + 1) >> 8;
		}
	}

	/* Show it */
	gtk_pixmap_get(GTK_PIXMAP(grad_view), &pmap, NULL);
	gdk_draw_rgb_image(pmap, grad_view->style->black_gc,
		0, 0, GP_WIDTH, GP_HEIGHT,
		GDK_RGB_DITHER_NONE, rgb, GP_WIDTH * 3);
	gtk_widget_queue_draw(grad_view);
}

void toolbar_update_settings()
{
	char txt[32];

	if ( toolbar_boxes[TOOLBAR_SETTINGS] == NULL ) return;

	ts_update_spinslides();		// Update tool settings
	ts_update_gradient();

	if ( mem_img_bpp == 1 )
		snprintf(txt, 30, "A [%i] = {%i,%i,%i}", mem_col_A, mem_pal[mem_col_A].red,
			mem_pal[mem_col_A].green, mem_pal[mem_col_A].blue );
	else	snprintf(txt, 30, "A = {%i,%i,%i}", mem_col_A24.red,
			mem_col_A24.green, mem_col_A24.blue );
	gtk_label_set_text( GTK_LABEL(toolbar_labels[0]), txt );

	if ( mem_img_bpp == 1 )
		snprintf(txt, 30, "B [%i] = {%i,%i,%i}", mem_col_B, mem_pal[mem_col_B].red,
			mem_pal[mem_col_B].green, mem_pal[mem_col_B].blue );
	else	snprintf(txt, 30, "B = {%i,%i,%i}", mem_col_B24.red,
			mem_col_B24.green, mem_col_B24.blue );
	gtk_label_set_text( GTK_LABEL(toolbar_labels[1]), txt );

	if ( mem_channel == CHN_IMAGE )
		gtk_label_set_text( GTK_LABEL(tb_label_opacity), _("Opacity") );
	else
		gtk_label_set_text( GTK_LABEL(tb_label_opacity), channames[mem_channel] );
}

static gboolean expose_palette(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int y2 = event->area.y + event->area.height;

	if (y2 > PALETTE_HEIGHT) /* Better safe than sorry */
	{
		gdk_draw_rectangle(widget->window, widget->style->black_gc,
			TRUE, event->area.x, PALETTE_HEIGHT,
			event->area.width, y2 - PALETTE_HEIGHT);
		if (event->area.y >= PALETTE_HEIGHT) return (TRUE);
		y2 = PALETTE_HEIGHT;
	}

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		event->area.x, event->area.y,
		event->area.width, y2 - event->area.y,
		GDK_RGB_DITHER_NONE, mem_pals + event->area.y * PALETTE_W3 +
		event->area.x * 3, PALETTE_W3);

	return (TRUE);
}


static gboolean motion_palette(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	GdkModifierType state;
	int x, y, pindex;

	if (event->is_hint) gdk_window_get_pointer (event->window, &x, &y, &state);
	else
	{
		x = event->x;
		y = event->y;
		state = event->state;
	}

	pindex = (y - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	pindex = pindex < 0 ? 0 : pindex >= mem_cols ? mem_cols - 1 : pindex;

	if (drag_index && (drag_index_vals[1] != pindex))
	{
		mem_pal_index_move(drag_index_vals[1], pindex);
		init_pal();
		drag_index_vals[1] = pindex;
	}

	return (TRUE);
}

static gint release_palette( GtkWidget *widget, GdkEventButton *event )
{
	if (drag_index)
	{
		drag_index = FALSE;
		gdk_window_set_cursor( drawing_palette->window, NULL );
		if ( drag_index_vals[0] != drag_index_vals[1] )
		{
			mem_pal_copy( mem_pal, brcosa_pal );		// Get old values back
			mem_undo_next(UNDO_XPAL);			// Do undo stuff
			mem_pal_index_move( drag_index_vals[0], drag_index_vals[1] );

			if ( mem_img_bpp == 1 )
				mem_canvas_index_move( drag_index_vals[0], drag_index_vals[1] );

			canvas_undo_chores();
		}
	}

	return FALSE;
}

static gint click_palette( GtkWidget *widget, GdkEventButton *event )
{
	int px, py, pindex;


	/* Filter out multiple clicks */
	if (event->type != GDK_BUTTON_PRESS) return (TRUE);

	px = event->x;
	py = event->y;
	if (py < PALETTE_SWATCH_Y) return (TRUE);
	pindex = (py - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	if (pindex >= mem_cols) return (TRUE);

	if (px < PALETTE_SWATCH_X) colour_selector(COLSEL_EDIT_ALL + pindex);
	else if (px < PALETTE_CROSS_X)		// Colour A or B changed
	{
		if (event->button == 1)
		{
			if (event->state & GDK_CONTROL_MASK)
			{
				mem_col_B = pindex;
				mem_col_B24 = mem_pal[mem_col_B];
			}
			else if (event->state & GDK_SHIFT_MASK)
			{
				mem_pal_copy(brcosa_pal, mem_pal);
				drag_index = TRUE;
				drag_index_vals[0] = pindex;
				drag_index_vals[1] = pindex;
				gdk_window_set_cursor(drawing_palette->window, move_cursor);
			}
			else
			{
				mem_col_A = pindex;
				mem_col_A24 = mem_pal[mem_col_A];
			}
		}
		else if (event->button == 3)
		{
			mem_col_B = pindex;
			mem_col_B24 = mem_pal[mem_col_B];
		}

		repaint_top_swatch();
		init_pal();
		gtk_widget_queue_draw(drawing_col_prev);	// Update widget
	}
	else /* if (px >= PALETTE_CROSS_X) */		// Mask changed
	{
		mem_prot_mask[pindex] ^= 255;

		/* Update & redraw swatch */
		repaint_swatch(pindex);
		gtk_widget_queue_draw_area(widget,
			PALETTE_CROSS_X, PALETTE_SWATCH_Y + PALETTE_SWATCH_H * pindex,
			PALETTE_WIDTH - PALETTE_CROSS_X, PALETTE_SWATCH_H);

		mem_mask_init();		// Prepare RGB masks
		/* !!! Do the same for any other kind of preview */
		if ((tool_type == TOOL_SELECT) && (marq_status >= MARQUEE_PASTE))
			update_all_views();
	}

	return (TRUE);
}

void toolbar_palette_init(GtkWidget *box)		// Set up the palette area
{
	GtkWidget *vbox, *hbox, *scrolledwindow_palette, *viewport_palette;


	toolbar_boxes[TOOLBAR_PALETTE] = vbox = pack(box, gtk_vbox_new(FALSE, 0));
	if (toolbar_status[TOOLBAR_PALETTE]) gtk_widget_show(vbox);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 5);

	drawing_col_prev = gtk_drawing_area_new();
	gtk_widget_show(drawing_col_prev);
	gtk_widget_set_usize(drawing_col_prev, PREVIEW_WIDTH, PREVIEW_HEIGHT);

	gtk_signal_connect_object( GTK_OBJECT(drawing_col_prev), "button_release_event",
		GTK_SIGNAL_FUNC (click_colours), GTK_OBJECT(drawing_col_prev) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_col_prev), "expose_event",
		GTK_SIGNAL_FUNC (expose_preview), GTK_OBJECT(drawing_col_prev) );
	gtk_widget_set_events (drawing_col_prev, GDK_ALL_EVENTS_MASK);

	viewport_palette = gtk_viewport_new (NULL, NULL);
	gtk_widget_show (viewport_palette);
	gtk_viewport_set_shadow_type( GTK_VIEWPORT(viewport_palette), GTK_SHADOW_IN );
	gtk_container_add (GTK_CONTAINER (viewport_palette), drawing_col_prev);
	gtk_box_pack_start( GTK_BOX(hbox), viewport_palette, TRUE, FALSE, 0 );

	scrolledwindow_palette = xpack(vbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show (scrolledwindow_palette);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow_palette),
		GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	drawing_palette = gtk_drawing_area_new();
	gtk_widget_show(drawing_palette);
	gtk_widget_set_usize(drawing_palette, PALETTE_WIDTH, 64);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolledwindow_palette),
		drawing_palette);
	fix_scroll(scrolledwindow_palette);

	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "expose_event",
		GTK_SIGNAL_FUNC (expose_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "button_press_event",
		GTK_SIGNAL_FUNC (click_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "motion_notify_event",
		GTK_SIGNAL_FUNC (motion_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "button_release_event",
		GTK_SIGNAL_FUNC (release_palette), GTK_OBJECT(drawing_palette) );

	gtk_widget_set_events (drawing_palette, GDK_ALL_EVENTS_MASK);
}

void toolbar_exit()				// Remember toolbar settings on program exit
{
	int i, x, y;
	char txt[32];

	for ( i=1; i<TOOLBAR_MAX; i++ )		// Remember current show/hide status
	{
		sprintf(txt, "toolbar%i", i);
		inifile_set_gboolean( txt, toolbar_status[i] );
	}

	if ( toolbar_boxes[TOOLBAR_SETTINGS] == NULL ) return;

	gdk_window_get_root_origin( toolbar_boxes[TOOLBAR_SETTINGS]->window, &x, &y );
	
	inifile_set_gint32("toolbar_settings_x", x );
	inifile_set_gint32("toolbar_settings_y", y );

	gtk_widget_destroy(toolbar_boxes[TOOLBAR_SETTINGS]);

	toolbar_boxes[TOOLBAR_SETTINGS] = NULL;
}


void toolbar_showhide()				// Show/Hide all 4 toolbars
{
	int i, bar[] = { TOOLBAR_MAIN, TOOLBAR_TOOLS, TOOLBAR_PALETTE, TOOLBAR_STATUS };

	if ( toolbar_boxes[TOOLBAR_MAIN] == NULL ) return;		// Grubby hack to avoid segfault

	for ( i=0; i<4; i++ )
	{
		if ( toolbar_status[ bar[i] ] )
			gtk_widget_show( toolbar_boxes[ bar[i] ] );
		else
			gtk_widget_hide( toolbar_boxes[ bar[i] ] );
	}

	if ( !toolbar_status[TOOLBAR_SETTINGS] )
	{
		toolbar_exit();
	}
	else
	{
		toolbar_settings_init();
		gdk_window_raise( main_window->window );
	}
}


void pressed_toolbar_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item )
{						// Menu toggle for toolbars
	toolbar_status[item] = GTK_CHECK_MENU_ITEM(menu_item)->active;;
	toolbar_showhide();
}



///	PATTERNS/TOOL PREVIEW AREA


void mem_set_brush(int val)			// Set brush, update size/flow/preview
{
	int offset, i, j, k, o, o2;

	tool_type = mem_brush_list[val][0];
	tool_size = mem_brush_list[val][1];
	if ( mem_brush_list[val][2]>0 ) tool_flow = mem_brush_list[val][2];

	offset = 3*( 2 + 36*(val % 9) + 36*PATCH_WIDTH*(val / 9) + 2*PATCH_WIDTH );
			// Offset in brush RGB
	for ( j=0; j<32; j++ )
	{
		o = 3*(40 + PREVIEW_WIDTH*j);		// Preview offset
		o2 = offset + 3*PATCH_WIDTH*j;		// Offset in brush RGB
		for ( i=0; i<32; i++ )
		{
			for ( k=0; k<3; k++ )
				mem_prev[o + 3*i + k] = mem_brushes[o2 + 3*i + k];
		}
	}

	if ( drawing_col_prev ) gtk_widget_queue_draw( drawing_col_prev );
}

void mem_pat_update()			// Update indexed and then RGB pattern preview
{
	int i, j, k, l;

	if ( mem_img_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}
	mem_pattern = (unsigned char *)mem_patterns[tool_pat];

	for (i = 0; i < 8 * 8; i++)
	{
		j = mem_pattern[i] ^ 1;
		mem_col_pat[i] = mem_col_[j];
		mem_col_pat24[i * 3 + 0] = mem_col_24[j].red;
		mem_col_pat24[i * 3 + 1] = mem_col_24[j].green;
		mem_col_pat24[i * 3 + 2] = mem_col_24[j].blue;
	}

	k = PREVIEW_WIDTH * 32 * 3;
	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < PREVIEW_WIDTH; j++ , k += 3)
		{
			l = ((i & 7) * 8 + (j & 7)) * 3;
			mem_prev[k + 0] = mem_col_pat24[l + 0];
			mem_prev[k + 1] = mem_col_pat24[l + 1];
			mem_prev[k + 2] = mem_col_pat24[l + 2];
		}
	}

	grad_def_update();
	if ((tool_type == TOOL_GRADIENT) && grad_opacity)
		gtk_widget_queue_draw(drawing_canvas);
}

void repaint_top_swatch()			// Update selected colours A & B
{
	int i, j, r[2], g[2], b[2], nx, ny;

	if ( mem_img_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}
	r[0] = mem_col_A24.red;
	g[0] = mem_col_A24.green;
	b[0] = mem_col_A24.blue;
	r[1] = mem_col_B24.red;
	g[1] = mem_col_B24.green;
	b[1] = mem_col_B24.blue;

	for ( j=0; j<20; j++ )
	{
		for ( i=0; i<20; i++ )
		{
			nx = i+1; ny = j+1;
			mem_prev[ 0 + 3*( nx + ny*PREVIEW_WIDTH) ] = r[0];
			mem_prev[ 1 + 3*( nx + ny*PREVIEW_WIDTH) ] = g[0];
			mem_prev[ 2 + 3*( nx + ny*PREVIEW_WIDTH) ] = b[0];

			nx = i+11; ny = j+11;
			mem_prev[ 0 + 3*( nx + ny*PREVIEW_WIDTH) ] = r[1];
			mem_prev[ 1 + 3*( nx + ny*PREVIEW_WIDTH) ] = g[1];
			mem_prev[ 2 + 3*( nx + ny*PREVIEW_WIDTH) ] = b[1];
		}
	}

	if ( drawing_col_prev ) gtk_widget_queue_draw( drawing_col_prev );
}
