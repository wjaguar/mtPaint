/*	layer.c
	Copyright (C) 2005-2007 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "layer.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "inifile.h"
#include "global.h"
#include "viewer.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"


int	layers_total = 0,		// Layers currently being used
	layer_selected = 0,		// Layer currently selected in the layers window
	layers_changed = 0;		// 0=Unchanged

char layers_filename[PATHBUF];		// Current filename for layers file
int	show_layers_main,		// Show all layers in main window
	layers_pastry_cut;		// Pastry cut layers in view area (for animation previews)


layer_node layer_table[MAX_LAYERS+1];	// Table of layer info


void layers_init()
{
	strncpy0(layer_table[0].name, _("Background"), LAYER_NAMELEN);
	layer_table[0].visible = TRUE;
	layer_table[0].use_trans = FALSE;
	layer_table[0].x = 0;
	layer_table[0].y = 0;
	layer_table[0].trans = 0;
	layer_table[0].opacity = 100;
}

/* Allocate layer image, its channels and undo stack */
static layer_image *alloc_layer(int w, int h, int bpp, int cmask, chanlist src)
{
	layer_image *lim;

	lim = calloc(1, sizeof(layer_image));
	if (!lim) return (NULL);
	if (init_undo(&lim->image_.undo_, MAX_UNDO) &&
		mem_alloc_image(&lim->image_, w, h, bpp, cmask, src))
		return (lim);
	free(lim->image_.undo_.items);
	free(lim);
	return (NULL);
}

static void repaint_layer(int l)	// Repaint layer in view/main window
{
	image_info *image;
	int lx, ly, lw, lh;

	lx = layer_table[l].x;
	ly = layer_table[l].y;
	image = l == layer_selected ? &mem_image :
		&layer_table[l].image->image_;
	lw = image->width;
	lh = image->height;
	if (layer_selected)
	{
		lx -= layer_table[layer_selected].x;
		ly -= layer_table[layer_selected].y;
	}

	vw_update_area(lx, ly, lw, lh);
	if (!show_layers_main) return;

	main_update_area(lx, ly, lw, lh);
}


///	LAYERS WINDOW

GtkWidget *layers_window = NULL;

typedef struct {
	GtkWidget *item, *name, *toggle;
} layer_item;

static GtkWidget *layer_list, *entry_layer_name,
	*layer_tools[TOTAL_ICONS_LAYER], *layer_spin, *layer_slider,
	*layer_label_position, *layer_trans_toggle, *layer_show_toggle;
static layer_item layer_list_data[MAX_LAYERS + 1];

gboolean layers_initialized;		// Indicates if initializing is complete



static void layers_update_titlebar()		// Update filename in titlebar
{
	char txt[300], txt2[PATHTXT], *extra = "-";

	if ( layers_window == NULL ) return;		// Don't bother if window is not showing

	gtkuncpy(txt2, layers_filename, PATHTXT);

	if ( layers_changed == 1 ) extra = _("(Modified)");

	snprintf( txt, 290, "%s %s %s", _("Layers"), extra, txt2[0] ? txt2 :
		_("Untitled"));

	gtk_window_set_title (GTK_WINDOW (layers_window), txt );
}

void layers_notify_changed()			// Layers have just changed - update vars as needed
{
	if ( layers_changed != 1 )
	{
		layers_changed = 1;
		layers_update_titlebar();
	}
}

static void layers_notify_unchanged()		// Layers have just been unchanged (saved) - update vars as needed
{
	if ( layers_changed != 0 )
	{
		layers_changed = 0;
		layers_update_titlebar();
	}
}


static void layer_copy_from_main( int l )	// Copy info from main image to layer
{
	layer_image *lp = layer_table[l].image;

	lp->image_ = mem_image;
	lp->state_ = mem_state;
}

static void layer_copy_to_main( int l )		// Copy info from layer to main image
{
	layer_image *lp = layer_table[l].image;

	mem_image = lp->image_;
	mem_state = lp->state_;
}

static void shift_layer(int val)
{
	layer_node temp = layer_table[layer_selected];
	int i, j, k;

	layer_table[layer_selected] = layer_table[layer_selected+val];
	layer_table[layer_selected+val] = temp;

	layer_selected += val;

		// Updated 2 list items - Text name + visible toggle
	mtMIN(j, layer_selected, layer_selected-val)
	mtMAX(k, layer_selected, layer_selected-val)
	for ( i=j; i<=k; i++ )
	{
		gtk_label_set_text( GTK_LABEL(layer_list_data[i].name), layer_table[i].name );
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( layer_list_data[i].toggle ),
				layer_table[i].visible);
	}

	gtk_list_select_child( GTK_LIST(layer_list), layer_list_data[layer_selected].item );
	layers_notify_changed();
	if ( layer_selected == layers_total )
		gtk_widget_set_sensitive(layer_tools[LTB_RAISE], FALSE);
	if ( layer_selected == 0 )
		gtk_widget_set_sensitive(layer_tools[LTB_LOWER], FALSE);

	if ( val==1 ) gtk_widget_set_sensitive(layer_tools[LTB_LOWER], TRUE);
	if ( val==-1 ) gtk_widget_set_sensitive(layer_tools[LTB_RAISE], TRUE);

	update_cols();				// Update status bar info

	if ((layer_selected == 0) || (layer_selected == val))
	{
		if (view_showing) gtk_widget_queue_draw(vw_drawing);
		if (show_layers_main) gtk_widget_queue_draw(drawing_canvas);
	}
	else
	{
		repaint_layer(layer_selected);			// Update View window
	}
}

static gint layer_press_raise()
{
	shift_layer(1);
	return FALSE;
}

static gint layer_press_lower()
{
	shift_layer(-1);
	return FALSE;
}


static void layer_new_chores(int l, layer_image *lim)
{
	if ( marq_status > MARQUEE_NONE )	// If we are selecting or pasting - lose it!
	{
		pressed_select_none(NULL, NULL);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
			icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
	}

	layer_table[l].image = lim;		// Image info memory pointer

	layer_table[l].name[0] = 0;
	layer_table[l].x = 0;
	layer_table[l].y = 0;
	layer_table[l].trans = 0;
	layer_table[l].opacity = 100;
	layer_table[l].visible = TRUE;
	layer_table[l].use_trans = FALSE;

	lim->state_.channel = lim->image_.img[mem_channel] ? mem_channel : CHN_IMAGE;

	lim->state_.col_[0] = 1;
	lim->state_.col_[1] = 0;
	lim->state_.col_24[0] = lim->image_.pal[1];
	lim->state_.col_24[1] = lim->image_.pal[0];

	lim->state_.xpm_trans = -1;
	lim->state_.xbm_hot_x = -1;
	lim->state_.xbm_hot_y = -1;
}

static void layer_new_chores2( int l )
{
	if ( layers_window != NULL )
	{
		gtk_widget_show( layer_list_data[l].item );
		gtk_widget_set_sensitive( layer_list_data[l].item, TRUE );	// Enable list item

		gtk_label_set_text( GTK_LABEL(layer_list_data[l].name), layer_table[l].name );
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( layer_list_data[l].toggle ),
			layer_table[l].visible);

		gtk_list_select_child( GTK_LIST(layer_list), layer_list_data[l].item );
		gtk_widget_set_sensitive(layer_tools[LTB_RAISE], FALSE);
		gtk_widget_set_sensitive(layer_tools[LTB_LOWER], l != 1);

		if ( l == MAX_LAYERS )		// Hide new/duplicate if we have max layers
		{
			gtk_widget_set_sensitive(layer_tools[LTB_NEW], FALSE);
			gtk_widget_set_sensitive(layer_tools[LTB_DUP], FALSE);
		}
	}

	layers_notify_changed();
}


void layer_new( int w, int h, int type, int cols, int cmask )	// Types 1=indexed, 2=grey, 3=RGB
{
	int bpp;
	layer_image *lim;

	if ( layers_total>=MAX_LAYERS ) return;

	if ( layers_total == 0 )
	{
		layer_table[0].image = malloc( sizeof(layer_image) );
	}
	layer_copy_from_main( layer_selected );

	bpp = type == 2 ? 1 : type;	// Type 2 = greyscale indexed

	lim = alloc_layer(w, h, bpp, cmask, NULL);
	if (!lim)
	{
		memory_errors(1);
		return;
	}

	lim->image_.cols = cols;
	mem_pal_copy(lim->image_.pal, mem_pal_def); // Default
	if ( type == 2 )	// Greyscale
	{
#ifdef U_GUADALINEX
		mem_scale_pal(lim->image_.pal, 0, 255,255,255, cols - 1, 0,0,0);
#else
		mem_scale_pal(lim->image_.pal, 0, 0,0,0, cols - 1, 255,255,255);
#endif
	}
	update_undo(&lim->image_);

	layers_total++;
	layer_new_chores(layers_total, lim);
	layer_new_chores2(layers_total);
	layer_selected = layers_total;

	if ( layers_total == 1 ) ani_init();		// Start with fresh animation data if new
}

static gint layer_press_new()
{
	generic_new_window(1);	// Call image new routine which will in turn call layer_new if needed

	return FALSE;
}

static gint layer_press_duplicate()
{
	layer_image *lim, *ls;
	int w = mem_width, h = mem_height, bpp = mem_img_bpp;

	if ( layers_total>=MAX_LAYERS ) return FALSE;

	gtk_widget_set_sensitive( main_window, FALSE);		// Stop any user input
	if ( layers_window ) gtk_widget_set_sensitive( layers_window, FALSE);
			// This stops a nasty segfault if users does 2 quick duplicates

	if ( layers_total == 0 ) layer_table[0].image = malloc( sizeof(layer_image) );
	layer_copy_from_main( layer_selected );

	lim = alloc_layer(w, h, bpp, 0, mem_img);
	if (!lim)
	{
		memory_errors(1);
		goto end;
	}
	layers_total++;

	layer_table[layers_total] = layer_table[layer_selected];		// Copy layer info
	layer_table[layers_total].image = lim;
	ls = layer_table[layer_selected].image;

	lim->state_ = ls->state_;
	mem_pal_copy(lim->image_.pal, ls->image_.pal);
	lim->image_.cols = ls->image_.cols;

	// Copy across position data
	memcpy(lim->ani_pos, ls->ani_pos, sizeof(lim->ani_pos));

	layer_new_chores2( layers_total );
	layer_selected = layers_total;

end:
	if ( layers_window ) gtk_widget_set_sensitive( layers_window, TRUE);
	gtk_widget_set_sensitive( main_window, TRUE);		// Restart user input
	gtk_list_select_child( GTK_LIST(layer_list), layer_list_data[layer_selected].item );

	return FALSE;
}

static void layer_delete(int item)
{
	layer_image *lp = layer_table[item].image;
	int i;

	mem_free_image(&lp->image_, FREE_ALL);
	free(lp);

	if (item < layers_total)	// If deleted item is not at the end shuffle rest down
		for ( i=item; i<layers_total; i++ )
			layer_table[i] = layer_table[i+1];

	layers_total--;
	if ( layers_total == 0 )
	{
		free( layer_table[0].image );
	}

	layers_notify_changed();
	update_all_views();
}


static void layer_refresh_list()
{
	int i;

	if ( layers_window == NULL ) return;

	for ( i=0; i<=MAX_LAYERS; i++ )
	{
		if ( layers_total<i )		// Disable item
		{
			gtk_widget_hide( layer_list_data[i].item );
			gtk_widget_set_sensitive( layer_list_data[i].item, FALSE );
		}
		else
		{
			gtk_widget_show( layer_list_data[i].item );
			gtk_widget_set_sensitive( layer_list_data[i].item, TRUE );
			gtk_label_set_text( GTK_LABEL(layer_list_data[i].name), layer_table[i].name );
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( layer_list_data[i].toggle ),
					layer_table[i].visible);
		}
	}
	if ( layer_selected == layers_total )
		gtk_widget_set_sensitive(layer_tools[LTB_RAISE], FALSE);
	gtk_widget_set_sensitive(layer_tools[LTB_NEW], TRUE);
	gtk_widget_set_sensitive(layer_tools[LTB_DUP], TRUE);
}

static gint layer_press_delete()
{
	char txt[256];
	int i, to_go = layer_selected;

	snprintf(txt, 256, _("Do you really want to delete layer %i (%s) ?"),
		layer_selected, layer_table[layer_selected].name );

	i = alert_box( _("Warning"), txt, _("No"), _("Yes"), NULL );

	if ( i==2 )
	{
		i = check_for_changes();
		if ( i==2 || i==10 || i<10 )
		{
			gtk_list_select_child( GTK_LIST(layer_list),
				layer_list_data[layer_selected-1].item );
			while (gtk_events_pending()) gtk_main_iteration();

			layer_delete( to_go );
			layer_refresh_list();
		}
	}

	return FALSE;
}

static void layer_show_position()
{
	char txt[35];

	if ( layers_window == NULL ) return;

	sprintf(txt, "%i , %i", layer_table[layer_selected].x, layer_table[layer_selected].y);
	gtk_label_set_text( GTK_LABEL(layer_label_position), txt );
}

static gint layer_press_centre()
{
	layer_table[layer_selected].x = layer_table[0].image->image_.width / 2 -
		mem_width / 2;
	layer_table[layer_selected].y = layer_table[0].image->image_.height / 2 -
		mem_height / 2;
	layer_show_position();
	layers_notify_changed();
	update_all_views();

	return FALSE;
}

int layers_unsaved_tot()			// Return number of layers with no filenames
{
	int j = 0, k;

	for ( k=0; k<=layers_total; k++ )	// Check each layer for proper filename
	{
		j += !(k == layer_selected ? mem_filename[0] :
			layer_table[k].image->state_.filename[0]);
	}

	return j;
}

int layers_changed_tot()			// Return number of layers with changes
{
	image_state *state;
	int j, k;

	for (j = k =0; k <= layers_total; k++)	// Check each layer for mem_changed
	{
		state = k == layer_selected ? &mem_state :
			&layer_table[k].image->state_;
		j += state->changed;
		j += !state->filename[0];
	}

	return j;
}

int check_layers_for_changes()			// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED
{
	int i = -10, j = 0;
	char *warning = _("One or more of the layers contains changes that have not been saved.  Do you really want to lose these changes?");


	j = j + layers_changed_tot() + layers_changed;
	if ( j>0 )
		i = alert_box( _("Warning"), warning, _("Cancel Operation"), _("Lose Changes"), NULL );

	return i;
}

static void layer_update_filename( char *name )
{
	strncpy(layers_filename, name, PATHBUF);
	layers_changed = 1;		// Forces update of titlebar
	layers_notify_unchanged();
}

void string_chop( char *txt )
{
	if ( strlen(txt) > 0 )		// Chop off unwanted non ASCII characters at end
	{
		while ( strlen(txt)>0 && txt[strlen(txt)-1]<32 ) txt[strlen(txt)-1] = 0;
	}
}

int read_file_num(FILE *fp, char *txt)
{
	int i;

	if (!fgets(txt, 32, fp)) return -987654321;
	sscanf(txt, "%i", &i);

	return i;
}

static void layers_remove_all(); /* Forward declaration */

int load_layers( char *file_name )
{
	char tin[300], load_name[PATHBUF], *c;
	layer_image *lim2;
	int i, j, k, layers_to_read = -1, layer_file_version = -1, lfail = 0;
	int lplen = 0;
	FILE *fp;

	c = strrchr(file_name, DIR_SEP);
	if (c) lplen = c - file_name + 1;

		// Try to save text file, return -1 if failure
	if ((fp = fopen(file_name, "r")) == NULL) goto fail;

	if (!fgets(tin, 32, fp)) goto fail2;

	string_chop( tin );
	if ( strcmp( tin, LAYERS_HEADER ) != 0 ) goto fail2;		// Bad header

	i = read_file_num(fp, tin);
	if ( i==-987654321 ) goto fail2;
	layer_file_version = i;
	if ( i>LAYERS_VERSION ) goto fail2;		// Version number must be compatible

	i = read_file_num(fp, tin);
	if ( i==-987654321 ) goto fail2;
	layers_to_read = i < MAX_LAYERS ? i : MAX_LAYERS;

	if ( layers_total>0 ) layers_remove_all();	// Remove all current layers if any
	for ( i=0; i<=layers_to_read; i++ )
	{
		// Read filename, strip end chars & try to load (if name length > 0)
		fgets(tin, 256, fp);
		string_chop(tin);
		snprintf(load_name, PATHBUF, "%.*s%s", lplen, file_name, tin);
		k = 1;
		j = detect_image_format(load_name);
		if ((j > 0) && (j != FT_NONE) && (j != FT_LAYERS1))
			k = load_image(load_name, FS_PNG_LOAD, j) != 1;

		if (k) /* Failure - skip this layer */
		{
			for ( j=0; j<7; j++ ) read_file_num(fp, tin);
			lfail++;
			continue;
		}

		/* Load was successful */
		set_new_filename(load_name);

		lim2 = malloc( sizeof(layer_image) );
		if (!lim2)
		{
			memory_errors(1);	// We have run out of memory!
			layers_remove_all();	// Remove all layers - total meltdown!
			goto fail2;
		}
		layer_table[layers_total].image = lim2;

		init_istate(); /* Update image variables after load */
		layer_copy_from_main( layers_total );

		fgets(tin, 256, fp);
		string_chop(tin);
		strncpy0(layer_table[layers_total].name, tin, LAYER_NAMELEN);

		k = read_file_num(fp, tin);
		layer_table[layers_total].visible = k > 0;

		layer_table[layers_total].x = read_file_num(fp, tin);
		layer_table[layers_total].y = read_file_num(fp, tin);

		k = read_file_num(fp, tin);
		layer_table[layers_total].use_trans = k > 0;

		k = read_file_num(fp, tin);
		layer_table[layers_total].trans = k < 0 ? 0 : k > 255 ? 255 : k;

		k = read_file_num(fp, tin);
		layer_table[layers_total].opacity = k < 1 ? 1 : k > 100 ? 100 : k;

		layers_total++;

// !!! A brittle hack - have to find other way to stop mem_free_image() in loader
		// Bogus 1x1 image used
		mem_width = 1;
		mem_height = 1;
		memset(mem_img, 0, sizeof(chanlist));
		init_undo(&mem_image.undo_, MAX_UNDO);
		mem_undo_im_[0].img[CHN_IMAGE] = mem_img[CHN_IMAGE] = malloc(3);
	}
	if ( layers_total>0 )
	{
		layers_total--;

		/* Free unused mem_image */
		mem_free_image(&mem_image, FREE_ALL);

		layer_copy_to_main(layers_total);
		if ( layers_total == 0 )
		{	// You will need to free the memory holding first layer if just 1 was loaded
			free( layer_table[0].image );
		}
		layer_selected = layers_total;
		layer_refresh_list();
		if ( layers_window != NULL )
			gtk_list_select_child( GTK_LIST(layer_list),
				layer_list_data[layer_selected].item );
	}
	else layer_refresh_list();

	/* Read in animation data - only if all layers loaded OK
	 * (to do otherwise is likely to result in SIGSEGV) */
	if (!lfail) ani_read_file(fp);

	fclose(fp);
	layer_update_filename( file_name );

	update_cols();		// Update status bar info

	if (lfail) /* There were failures */
	{
		snprintf(tin, 300, _("%d layers failed to load"), lfail);
		alert_box( _("Error"), tin, _("OK"), NULL, NULL );
	}

	return 1;		// Success
fail2:
	fclose(fp);
fail:
	return -1;
}

/* Convert absolute filename 'file' into one relative to prefix */
static void parse_filename(char *dest, char *prefix, char *file, int len)
{
	int i, k;

	/* # of chars that match at start */
	for (i = 0; (i < len) && (prefix[i] == file[i]); i++);

	if (!i || (i == len)) /* Complete match, or no match at all */
		strncpy(dest, file + i, PATHBUF);
	else	/* Partial match */
	{
		dest[0] = 0;
		/* Count number of DIR_SEP encountered on and after point i in
		 * 'prefix', add a '../' for each found */
		for (k = i; k < len; k++)
			if (prefix[k] == DIR_SEP) strnncat( dest, "../", PATHBUF);
		/* nip backwards on 'file' from i to previous DIR_SEP or
		 * beginning and ... */
		for (k = i; (k >= 0) && (file[k] != DIR_SEP); k--);
		/* ... add rest of 'file' */
		strnncat(dest, file + k + 1, PATHBUF);
	}
}

void layer_press_save_composite()		// Create, save, free the composite image
{
	file_selector( FS_COMPOSITE_SAVE );
}

int layer_save_composite(char *fname, ls_settings *settings)
{
	image_info *image;
	unsigned char *layer_rgb;
	int w, h, res=0;

	image = layer_selected ? &layer_table[0].image->image_ : &mem_image;
	w = image->width;
	h = image->height;
	layer_rgb = malloc(w * h * 3);
	if (layer_rgb)
	{
		view_render_rgb(layer_rgb, 0, 0, w, h, 1);	// Render layer
		settings->img[CHN_IMAGE] = layer_rgb;
		settings->width = w;
		settings->height = h;
		settings->bpp = 3;
		if (layer_selected) /* Set up background transparency */
		{
			res = layer_table[0].image->state_.xpm_trans;
			settings->xpm_trans = res;
			settings->rgb_trans = res < 0 ? -1 :
				PNG_2_INT(layer_table[0].image->image_.pal[res]);

		}
		res = save_image(fname, settings);
		free( layer_rgb );
	}
	else memory_errors(1);

	return res;
}

int save_layers( char *file_name )
{
	char comp_name[PATHBUF], *c, *msg;
	int i, l = 0;
	FILE *fp;

	c = strrchr(file_name, DIR_SEP);
	if (c) l = c - file_name + 1;

		// Try to save text file, return -1 if failure
	if ((fp = fopen(file_name, "w")) == NULL) goto fail;

	fprintf( fp, "%s\n%i\n%i\n", LAYERS_HEADER, LAYERS_VERSION, layers_total );
	for ( i=0; i<=layers_total; i++ )
	{
		c = layer_selected == i ? mem_filename :
			layer_table[i].image->state_.filename;
		parse_filename(comp_name, file_name, c, l);
		fprintf( fp, "%s\n", comp_name );

		fprintf( fp, "%s\n%i\n%i\n%i\n%i\n%i\n%i\n",
			layer_table[i].name, layer_table[i].visible, layer_table[i].x,
			layer_table[i].y, layer_table[i].use_trans, layer_table[i].trans,
			layer_table[i].opacity);
	}

	ani_write_file(fp);			// Write animation data

	fclose(fp);
	layer_update_filename( file_name );
	register_file( file_name );		// Recently used file list / last directory

	return 1;		// Success
fail:
	c = gtkuncpy(NULL, layers_filename, 0);
	msg = g_strdup_printf(_("Unable to save file: %s"), c);
	alert_box(_("Error"), msg, _("OK"), NULL, NULL);
	g_free(msg);
	g_free(c);

	return -1;
}


int check_layers_all_saved()
{
	if ( layers_unsaved_tot() > 0 )
	{
		alert_box( _("Warning"), _("One or more of the image layers has not been saved.  You must save each image individually before saving the layers text file in order to load this composite image in the future."), _("OK"), NULL, NULL );
		return 1;
	}

	return 0;
}

void layer_press_save_as()
{
	check_layers_all_saved();
	file_selector( FS_LAYER_SAVE );
// Use standard file_selector which in turn calls save_layers( char *file_name );
}

void layer_press_save()
{
	if (!layers_filename[0]) layer_press_save_as();
	else
	{
		check_layers_all_saved();
		save_layers( layers_filename );
	}
}

static void update_main_with_new_layer()
{
	int w, h;

	canvas_size(&w, &h);
	gtk_widget_set_usize(drawing_canvas, w, h);
	vw_focus_view();
	update_all_views();

	init_pal();		// Update Palette, pattern & mask area + widgets
	gtk_widget_queue_draw(drawing_col_prev);

	update_titlebar();
	update_menus();
	if ((tool_type == TOOL_SMUDGE) && (MEM_BPP == 1))
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
}

static void layers_remove_all()
{
	int i;

	gtk_widget_set_sensitive( main_window, FALSE);		// Stop any user input
	if ( layers_window ) gtk_widget_set_sensitive( layers_window, FALSE);

	if ( layers_window !=0 )
	{
		gtk_list_select_child( GTK_LIST(layer_list), layer_list_data[0].item );
		while (gtk_events_pending()) gtk_main_iteration();
	}
	else
	{
		if ( layers_total>0 && layer_selected!=0 )	// Copy over layer 0
		{
			layer_copy_to_main(0);
			layer_selected = 0;
			update_main_with_new_layer();
		}
	}

	for ( i=layers_total; i>0; i-- )
	{
		layer_delete(i);
	}
	layer_refresh_list();
	layers_filename[0] = 0;
	layers_notify_unchanged();

	if ( layers_window ) gtk_widget_set_sensitive( layers_window, TRUE);
	update_image_bar();					// Update status bar
	gtk_widget_set_sensitive( main_window, TRUE);		// Restart user input
}

void layer_press_remove_all()
{
	int i;

	i = check_layers_for_changes();
	if (i < 0) i = alert_box( _("Warning"), _("Do you really want to delete all of the layers?"), _("No"), _("Yes"), NULL );
	if (i == 2) layers_remove_all();
}

static gint layer_tog_visible( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int j;

	if ( !layers_initialized ) return TRUE;

	j = (int)gtk_object_get_user_data(GTK_OBJECT(widget));

	if (j)
	{
		layer_table[j].visible =
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		layers_notify_changed();
		repaint_layer(j);
	}

	return FALSE;
}

static gint layer_main_toggled( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	show_layers_main = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(layer_show_toggle) );
	gtk_widget_queue_draw(drawing_canvas);

	return FALSE;
}

static void layer_inputs_changed()
{
	const char *nname;
	gboolean txt_changed = FALSE;

	if (!layers_initialized) return;

	layers_notify_changed();

	layer_table[layer_selected].trans =
			gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(layer_spin) );
	layer_table[layer_selected].opacity = mt_spinslide_get_value(layer_slider);

	nname = gtk_entry_get_text(GTK_ENTRY(entry_layer_name));
	if (strncmp(layer_table[layer_selected].name, nname, LAYER_NAMELEN - 1))
		txt_changed = TRUE;

	strncpy0(layer_table[layer_selected].name, nname, LAYER_NAMELEN);
	gtk_label_set_text( GTK_LABEL(layer_list_data[layer_selected].name),
		layer_table[layer_selected].name );
	layer_table[layer_selected].use_trans = gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(layer_trans_toggle));

	if ( !txt_changed ) repaint_layer( layer_selected );
		// Update layer image if not just changing text
}

void layer_choose( int l )				// Select a new layer from the list
{
	if ( l<=layers_total && l>=0 && l != layer_selected )	// Change selected layer if different
	{
		if ( layers_window == NULL )
		{
			layer_copy_from_main( layer_selected );
					// Copy image info to layer table before we change
			layer_selected = l;
			if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
				pressed_select_none( NULL, NULL );

			layer_copy_to_main( layer_selected );
			update_main_with_new_layer();
		}
		else
		{
			gtk_list_select_child( GTK_LIST(layer_list),
				layer_list_data[l].item );
		}
	}
}

static gint layer_select( GtkList *list, GtkWidget *widget, gpointer user_data )
{
	gboolean dont_update = FALSE;
	int j;

	if ( !layers_initialized ) return TRUE;

	layers_initialized = FALSE;

	j = (int)gtk_object_get_user_data(GTK_OBJECT(widget));

	if ( j==layer_selected )
		dont_update=TRUE;	// Already selected e.g. raise, lower, startup

	if (entry_layer_name && (j <= layers_total))
	{
		if ( !dont_update ) /* Move data before doing anything else */
		{
//			if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
//				pressed_select_none( NULL, NULL );
			layer_copy_from_main( layer_selected );
			layer_copy_to_main( layer_selected = j );
			update_main_with_new_layer();
		}

		gtk_entry_set_text( GTK_ENTRY(entry_layer_name), layer_table[j].name );
		if ( j==0 )		// Background layer selected
		{
			gtk_widget_set_sensitive(layer_tools[LTB_RAISE], layers_total > 0);
			gtk_widget_set_sensitive(layer_tools[LTB_LOWER], FALSE);
			gtk_widget_set_sensitive(layer_tools[LTB_DEL], FALSE);
			gtk_widget_set_sensitive(layer_tools[LTB_CENTER], FALSE);
			gtk_widget_set_sensitive( layer_trans_toggle, FALSE );
			gtk_widget_set_sensitive( layer_spin, FALSE );
			gtk_label_set_text( GTK_LABEL(layer_label_position), _("Background") );

			gtk_widget_set_sensitive( layer_slider, FALSE );
		}
		else
		{
			gtk_widget_set_sensitive( layer_slider, TRUE );
			mt_spinslide_set_value(layer_slider, layer_table[layer_selected].opacity);
			layer_show_position();
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( layer_trans_toggle ),
				layer_table[layer_selected].use_trans);

			gtk_widget_set_sensitive(layer_tools[LTB_RAISE], j != layers_total);
			gtk_widget_set_sensitive(layer_tools[LTB_LOWER], TRUE);
			gtk_widget_set_sensitive(layer_tools[LTB_DEL], TRUE);
			gtk_widget_set_sensitive(layer_tools[LTB_CENTER], TRUE);
			gtk_widget_set_sensitive( layer_trans_toggle, TRUE );

			gtk_widget_set_sensitive( layer_spin, TRUE );
			gtk_spin_button_set_value( GTK_SPIN_BUTTON(layer_spin), layer_table[j].trans);
		}
		
	}

	while (gtk_events_pending()) gtk_main_iteration();
	layers_initialized = TRUE;

	return FALSE;
}

gint delete_layers_window()
{
	if ( !GTK_WIDGET_SENSITIVE(layers_window) ) return TRUE;
		// Stop user prematurely exiting while drag 'n' drop loading

	win_store_pos(layers_window, "layers");

	gtk_widget_destroy(layers_window);
	gtk_widget_set_sensitive(menu_widgets[MENU_LAYER], TRUE);
	layers_window = NULL;

	return FALSE;
}

void pressed_paste_layer( GtkMenuItem *menu_item, gpointer user_data )
{
	int ol = layer_selected, new_type = CMASK_IMAGE, i, j, k;
	unsigned char *dest;

	/* No way to put RGB clipboard into utility channel */
	if ((mem_clip_bpp == 3) && (mem_channel != CHN_IMAGE)) return;

	if ( layers_total<MAX_LAYERS )
	{
		gtk_widget_set_sensitive( main_window, FALSE);		// Stop any user input
		if ( layers_window ) gtk_widget_set_sensitive( layers_window, FALSE);
				// This stops a nasty segfault if users does 2 quick paste layers

		if ((mem_clip_alpha || mem_clip_mask) && !channel_dis[CHN_ALPHA])
			new_type = CMASK_RGBA;
		new_type |= CMASK_FOR(mem_channel);
		layer_new(mem_clip_w, mem_clip_h, mem_clip_bpp, mem_cols, new_type);

		if ( layer_selected != ol )	// If == then new layer wasn't created
		{
			layer_table[layer_selected].x = mem_clip_x;
			layer_table[layer_selected].y = mem_clip_y;

			mem_pal_copy(layer_table[layer_selected].image->image_.pal,
				layer_table[ol].image->image_.pal);	// Copy palette

			layer_table[layer_selected].image->state_ =
				layer_table[ol].image->state_;

			layer_copy_to_main( layer_selected );
			update_main_with_new_layer();

			j = mem_clip_w * mem_clip_h;
			memcpy(mem_img[mem_channel], mem_clipboard, j * mem_clip_bpp);

			/* Image channel with alpha */
			if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA])
			{
				/* Fill alpha channel */
				if (mem_clip_alpha)
					memcpy(mem_img[CHN_ALPHA], mem_clip_alpha, j);
				else memset(mem_img[CHN_ALPHA], 255, j);
			}

			/* Image channel with mask */
			if ((mem_channel == CHN_IMAGE) && mem_clip_mask)
			{
				/* Mask image - fill unselected part with color A */
				dest = mem_img[CHN_IMAGE];
				k = mem_clip_bpp == 1 ? mem_col_A : mem_col_A24.red;
				for (i = 0; i < j; i++ , dest += mem_clip_bpp)
				{
					if (mem_clip_mask[i]) continue;
					dest[0] = k;
					if (mem_clip_bpp == 1) continue;
					dest[1] = mem_col_A24.green;
					dest[2] = mem_col_A24.blue;
				}
			}

			/* Utility channel with mask */
			dest = mem_img[CHN_ALPHA];
			if (mem_channel != CHN_IMAGE) dest = mem_img[mem_channel];
			if (dest && mem_clip_mask)
			{
				/* Mask the channel */
				for (i = 0; i < j; i++)
				{
					k = dest[i] * mem_clip_mask[i];
					dest[i] = (k + (k >> 8) + 1) >> 8;
				}
			}

//			if ( layers_window == NULL ) pressed_layers( NULL, NULL );
			if ( !view_showing ) view_show();

		}
		if ( layers_window ) gtk_widget_set_sensitive( layers_window, TRUE);
		gtk_widget_set_sensitive( main_window, TRUE);		// Restart user input
	}
	else alert_box( _("Error"), _("You cannot add any more layers."), _("OK"), NULL, NULL );
}

void move_layer_relative(int l, int change_x, int change_y)	// Move a layer & update window labels
{
	int lx = layer_table[l].x, ly = layer_table[l].y, lw, lh;

	layer_table[l].x += change_x;
	layer_table[l].y += change_y;
	if (change_x < 0) lx += change_x;
	if (change_y < 0) ly += change_y;
	if (l == layer_selected)
	{
		lw = mem_width;
		lh = mem_height;
		layer_show_position();
	}
	else
	{
		lw = layer_table[l].image->image_.width;
		lh = layer_table[l].image->image_.height;
	}
	layers_notify_changed();

	lw += abs(change_x);
	lh += abs(change_y);
	if (layer_selected)
	{
		lx -= layer_table[layer_selected].x;
		ly -= layer_table[layer_selected].y;
	}
	vw_update_area(lx, ly, lw, lh);
	if ( show_layers_main ) gtk_widget_queue_draw(drawing_canvas);
}

void pressed_layers( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *vbox, *hbox, *table, *label, *tog, *scrolledwindow, *item;
	GtkAccelGroup* ag = gtk_accel_group_new();
	char txt[32];
	int i;

	gtk_widget_set_sensitive(menu_widgets[MENU_LAYER], FALSE);

	entry_layer_name = NULL;
	layers_initialized = FALSE;


	layers_window = add_a_window( GTK_WINDOW_TOPLEVEL, "", GTK_WIN_POS_NONE, FALSE );
	win_restore_pos(layers_window, "layers", 0, 0, 400, 400);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (layers_window), vbox);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

	scrolledwindow = xpack(vbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show (scrolledwindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	layer_list = gtk_list_new ();
	gtk_signal_connect( GTK_OBJECT(layer_list), "select_child",
			GTK_SIGNAL_FUNC(layer_select), NULL );
	gtk_widget_show (layer_list);

	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(scrolledwindow), layer_list);

	for ( i=MAX_LAYERS; i>=0; i-- )
	{
		hbox = gtk_hbox_new(FALSE, 3);

		layer_list_data[i].item = item = gtk_list_item_new();
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		gtk_container_add(GTK_CONTAINER(layer_list), item);
		gtk_container_add(GTK_CONTAINER(item), hbox);

		sprintf(txt, "%i", i);
		label = pack(hbox, gtk_label_new(txt));
		gtk_widget_set_usize (label, 40, -2);
		gtk_misc_set_alignment( GTK_MISC(label), 0.5, 0.5 );

		label = xpack(hbox, gtk_label_new(""));
		gtk_misc_set_alignment( GTK_MISC(label), 0, 0.5 );
		layer_list_data[i].name = label;

		tog = pack(hbox, gtk_check_button_new_with_label(""));
		gtk_object_set_user_data(GTK_OBJECT(tog), (gpointer)i);
		layer_list_data[i].toggle = tog;
		gtk_widget_show_all(item);
		if ( i == 0 ) gtk_widget_hide(tog);
		else gtk_signal_connect(GTK_OBJECT(tog), "clicked",
			GTK_SIGNAL_FUNC(layer_tog_visible), NULL);
	}

	for ( i=0; i<=MAX_LAYERS; i++ )
	{
		if ( i<=layers_total )
		{
			gtk_label_set_text( GTK_LABEL(layer_list_data[i].name), layer_table[i].name );
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( layer_list_data[i].toggle ),
				layer_table[i].visible);
		}
		else
		{
			gtk_widget_hide( layer_list_data[i].item );
			gtk_widget_set_sensitive( layer_list_data[i].item, FALSE );
			layer_table[i].image = NULL;		// Needed for checks later
		}
	}

	pack(vbox, layer_toolbar(layer_tools));

	if ( layers_total == MAX_LAYERS )	// Hide new/duplicate if we have max layers
	{
		gtk_widget_set_sensitive(layer_tools[LTB_NEW], FALSE);
		gtk_widget_set_sensitive(layer_tools[LTB_DUP], FALSE );
	}

	table = add_a_table( 3, 2, 5, vbox );
	gtk_table_set_row_spacings (GTK_TABLE (table), 5);
	gtk_table_set_col_spacings (GTK_TABLE (table), 5);

	add_to_table( _("Layer Name"), table, 0, 0, 0 );
	add_to_table( _("Position"), table, 1, 0, 0 );
	add_to_table( _("Opacity"), table, 2, 0, 0 );

	entry_layer_name = gtk_entry_new_with_max_length(LAYER_NAMELEN - 1);
	gtk_widget_set_usize(entry_layer_name, 100, -2);
	gtk_widget_show (entry_layer_name);
	gtk_table_attach (GTK_TABLE (table), entry_layer_name, 1, 2, 0, 1,
		(GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_signal_connect( GTK_OBJECT(entry_layer_name),
			"changed", GTK_SIGNAL_FUNC(layer_inputs_changed), NULL);

	layer_label_position = add_to_table( "-320, 200", table, 1, 1, 1 );

	layer_slider = mt_spinslide_new(-2, -2);
	mt_spinslide_set_range(layer_slider, 0, 100);
	gtk_table_attach(GTK_TABLE(table), layer_slider, 1, 2, 2, 3,
		GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
	mt_spinslide_connect(layer_slider, GTK_SIGNAL_FUNC(layer_inputs_changed), NULL);
	mt_spinslide_set_value(layer_slider, layer_table[layer_selected].opacity);

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	layer_trans_toggle = add_a_toggle( _("Transparent Colour"), hbox, TRUE );
	gtk_signal_connect(GTK_OBJECT(layer_trans_toggle), "clicked",
			GTK_SIGNAL_FUNC(layer_inputs_changed), NULL);

	layer_spin = pack(hbox, add_a_spin(0, 0, 255));
	spin_connect(layer_spin, GTK_SIGNAL_FUNC(layer_inputs_changed), NULL);

	layer_show_toggle = add_a_toggle( _("Show all layers in main window"), vbox, show_layers_main );
	gtk_signal_connect(GTK_OBJECT(layer_show_toggle), "clicked",
			GTK_SIGNAL_FUNC(layer_main_toggled), NULL);

	gtk_widget_add_accelerator(layer_tools[LTB_CLOSE], "clicked", ag,
		GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_signal_connect_object (GTK_OBJECT (layers_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_layers_window), NULL);

	gtk_window_set_transient_for( GTK_WINDOW(layers_window), GTK_WINDOW(main_window) );
	gtk_widget_show(layers_window);
	gtk_window_add_accel_group(GTK_WINDOW (layers_window), ag);

	layers_initialized = TRUE;
	gtk_list_select_child( GTK_LIST(layer_list), layer_list_data[layer_selected].item );

	layers_update_titlebar();
}


void layer_iconbar_click(GtkWidget *widget, gpointer data)
{
	gint j = (gint) data;

	switch (j)
	{
		case 0:	layer_press_new(); break;
		case 1:	layer_press_raise(); break;
		case 2:	layer_press_lower(); break;
		case 3:	layer_press_duplicate(); break;
		case 4:	layer_press_centre(); break;
		case 5:	layer_press_delete(); break;
		case 6:	delete_layers_window(NULL,NULL,NULL); break;
	}
}
