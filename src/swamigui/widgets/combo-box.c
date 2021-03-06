/* 
 *
 * Ripped and slightly modified for Swami from libgal-0.19.2
 *
 * gtk-combo-box.c - a customizable combobox
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Adrian E Feiguin (feiguin@ifir.edu.ar)
 *   Paolo Molnaro (lupus@debian.org).
 *   Jon K Hellan (hellan@acm.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <gtk/gtkhbox.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkarrow.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtktearoffmenuitem.h>
#include <gdk/gdkkeysyms.h>
#include "combo-box.h"

static GtkHBoxClass *combo_box_parent_class;
static int combo_toggle_pressed (GtkToggleButton * tbutton,
				 ComboBox * combo_box);
static void combo_popup_tear_off (ComboBox * combo, gboolean set_position);
static void combo_set_tearoff_state (ComboBox * combo, gboolean torn_off);
static void combo_popup_reparent (GtkWidget * popup, GtkWidget * new_parent,
				  gboolean unrealize);
static gboolean cb_popup_delete (GtkWidget * w, GdkEventAny * event,
				 ComboBox * combo);
static void combo_tearoff_bg_copy (ComboBox * combo);

enum
{
  POP_DOWN_WIDGET,
  POP_DOWN_DONE,
  PRE_POP_DOWN,
  POST_POP_HIDE,
  LAST_SIGNAL
};

static gint combo_box_signals[LAST_SIGNAL] = { 0, };

struct _ComboBoxPrivate
{
  GtkWidget *pop_down_widget;
  GtkWidget *display_widget;

  /*
   * Internal widgets used to implement the ComboBox
   */
  GtkWidget *frame;
  GtkWidget *arrow_button;

  GtkWidget *toplevel;		/* Popup's toplevel when not torn off */
  GtkWidget *tearoff_window;	/* Popup's toplevel when torn off */
  guint torn_off;

  GtkWidget *tearable;		/* The tearoff "button" */
  GtkWidget *popup;		/* Popup */

  /*
   * Closure for invoking the callbacks above
   */
  void *closure;
};

static void
combo_box_finalize (GObject *object)
{
  ComboBox *combo_box = COMBO_BOX (object);

  gtk_object_destroy (GTK_OBJECT (combo_box->priv->toplevel));
  g_object_unref (G_OBJECT (combo_box->priv->toplevel));
  if (combo_box->priv->tearoff_window)
    {
      gtk_object_destroy (GTK_OBJECT (combo_box->priv->tearoff_window));
      g_object_unref (G_OBJECT (combo_box->priv->tearoff_window));	/* ?? */
    }
  g_free (combo_box->priv);

  G_OBJECT_CLASS (combo_box_parent_class)->finalize (object);
}

void
my_marshal_POINTER__NONE (GClosure     *closure,
			  GValue       *return_value,
			  guint         n_param_values,
			  const GValue *param_values,
			  gpointer      invocation_hint,
			  gpointer      marshal_data)
{
  typedef gpointer (*GMarshalFunc_POINTER__NONE) (gpointer     data1,
						  gpointer     data2);
  register GMarshalFunc_POINTER__NONE callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2, retval;

  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_POINTER__NONE)
    (marshal_data ? marshal_data : cc->callback);

  retval = callback (data1, data2);
  g_value_set_pointer (return_value, retval);
}

static void
combo_box_class_init (ComboBoxClass *klass)
{
  combo_box_parent_class = gtk_type_class (gtk_hbox_get_type ());

  G_OBJECT_CLASS (klass)->finalize = combo_box_finalize;

  combo_box_signals[POP_DOWN_WIDGET] =
    g_signal_new ("pop_down_widget", G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ComboBoxClass, pop_down_widget), NULL, NULL,
		  my_marshal_POINTER__NONE, GTK_TYPE_POINTER, 0);

  combo_box_signals[POP_DOWN_DONE] =
    g_signal_new ("pop_down_done", G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ComboBoxClass, pop_down_done), NULL, NULL,
		  gtk_marshal_BOOL__POINTER, GTK_TYPE_BOOL, 1,
		  GTK_TYPE_OBJECT);

  combo_box_signals[PRE_POP_DOWN] =
    g_signal_new ("pre_pop_down", G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ComboBoxClass, pre_pop_down), NULL, NULL,
		  gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  combo_box_signals[POST_POP_HIDE] =
    g_signal_new ("post_pop_hide", G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ComboBoxClass, post_pop_hide), NULL, NULL,
		  gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
}

static void
deactivate_arrow (ComboBox * combo_box)
{
  GtkToggleButton *arrow;

  arrow = GTK_TOGGLE_BUTTON (combo_box->priv->arrow_button);
  gtk_signal_handler_block_by_func
    (GTK_OBJECT (arrow), GTK_SIGNAL_FUNC (combo_toggle_pressed), combo_box);

  gtk_toggle_button_set_active (arrow, FALSE);

  gtk_signal_handler_unblock_by_func
    (GTK_OBJECT (arrow), GTK_SIGNAL_FUNC (combo_toggle_pressed), combo_box);
}

/**
 * combo_box_popup_hide_unconditional
 * @combo_box:  Combo box
 *
 * Hide popup, whether or not it is torn off.
 */
static void
combo_box_popup_hide_unconditional (ComboBox * combo_box)
{
  gboolean popup_info_destroyed = FALSE;

  g_return_if_fail (combo_box != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo_box));

  gtk_widget_hide (combo_box->priv->toplevel);
  gtk_widget_hide (combo_box->priv->popup);
  if (combo_box->priv->torn_off)
    {
      GTK_TEAROFF_MENU_ITEM (combo_box->priv->tearable)->torn_off = FALSE;
      combo_set_tearoff_state (combo_box, FALSE);
    }

  gtk_grab_remove (combo_box->priv->toplevel);
  gdk_pointer_ungrab (GDK_CURRENT_TIME);

  g_object_ref (G_OBJECT (combo_box->priv->pop_down_widget));
  gtk_signal_emit (GTK_OBJECT (combo_box),
		   combo_box_signals[POP_DOWN_DONE],
		   combo_box->priv->pop_down_widget, &popup_info_destroyed);

  if (popup_info_destroyed)
    {
      gtk_container_remove (GTK_CONTAINER (combo_box->priv->frame),
			    combo_box->priv->pop_down_widget);
      combo_box->priv->pop_down_widget = NULL;
    }
  g_object_unref (G_OBJECT (combo_box->priv->pop_down_widget));
  deactivate_arrow (combo_box);

  gtk_signal_emit (GTK_OBJECT (combo_box), combo_box_signals[POST_POP_HIDE]);
}

/**
 * combo_box_popup_hide:
 * @combo_box:  Combo box
 *
 * Hide popup, but not when it is torn off.
 * This is the external interface - for subclasses and apps which expect a
 * regular combo which doesn't do tearoffs.
 */
void
combo_box_popup_hide (ComboBox * combo_box)
{
  if (!combo_box->priv->torn_off)
    combo_box_popup_hide_unconditional (combo_box);
  else if (GTK_WIDGET_VISIBLE (combo_box->priv->toplevel))
    {
      /* Both popup and tearoff window present. Get rid of just
         the popup shell. */
      combo_popup_tear_off (combo_box, FALSE);
      deactivate_arrow (combo_box);
    }
}

/*
 * Find best location for displaying
 */
void
combo_box_get_pos (ComboBox * combo_box, int *x, int *y)
{
  GtkWidget *wcombo = GTK_WIDGET (combo_box);
  int ph, pw;

  gdk_window_get_origin (wcombo->window, x, y);
  *y += wcombo->allocation.height + wcombo->allocation.y;
  *x += wcombo->allocation.x;

  ph = combo_box->priv->popup->allocation.height;
  pw = combo_box->priv->popup->allocation.width;

  if ((*y + ph) > gdk_screen_height ())
    *y = gdk_screen_height () - ph;

  if ((*x + pw) > gdk_screen_width ())
    *x = gdk_screen_width () - pw;
}

static void
combo_box_popup_display (ComboBox * combo_box)
{
  int x, y;

  g_return_if_fail (combo_box != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo_box));

  /*
   * If we have no widget to display on the popdown,
   * create it
   */
  if (!combo_box->priv->pop_down_widget)
    {
      GtkWidget *pw = NULL;

      gtk_signal_emit (GTK_OBJECT (combo_box),
		       combo_box_signals[POP_DOWN_WIDGET], &pw);
      g_assert (pw != NULL);
      combo_box->priv->pop_down_widget = pw;
      gtk_container_add (GTK_CONTAINER (combo_box->priv->frame), pw);
    }

  gtk_signal_emit (GTK_OBJECT (combo_box), combo_box_signals[PRE_POP_DOWN]);

  if (combo_box->priv->torn_off)
    {
      /* To give the illusion that tearoff still displays the
       * popup, we copy the image in the popup window to the
       * background. Thus, it won't be blank after reparenting */
      combo_tearoff_bg_copy (combo_box);

      /* We force an unrealize here so that we don't trigger
       * redrawing/ clearing code - we just want to reveal our
       * backing pixmap.
       */
      combo_popup_reparent (combo_box->priv->popup,
			    combo_box->priv->toplevel, TRUE);
    }

  combo_box_get_pos (combo_box, &x, &y);

  gtk_widget_set_uposition (combo_box->priv->toplevel, x, y);
  gtk_widget_realize (combo_box->priv->popup);
  gtk_widget_show (combo_box->priv->popup);
  gtk_widget_realize (combo_box->priv->toplevel);
  gtk_widget_show (combo_box->priv->toplevel);

  gtk_grab_add (combo_box->priv->toplevel);
  gdk_pointer_grab (combo_box->priv->toplevel->window, TRUE,
		    GDK_BUTTON_PRESS_MASK |
		    GDK_BUTTON_RELEASE_MASK |
		    GDK_POINTER_MOTION_MASK, NULL, NULL, GDK_CURRENT_TIME);
}

static int
combo_toggle_pressed (GtkToggleButton * tbutton, ComboBox * combo_box)
{
  if (tbutton->active)
    combo_box_popup_display (combo_box);
  else
    combo_box_popup_hide_unconditional (combo_box);

  return TRUE;
}

static gint
combo_box_button_press (GtkWidget * widget, GdkEventButton * event,
			ComboBox * combo_box)
{
  GtkWidget *child;

  child = gtk_get_event_widget ((GdkEvent *) event);
  if (child != widget)
    {
      while (child)
	{
	  if (child == widget)
	    return FALSE;
	  child = child->parent;
	}
    }

  combo_box_popup_hide (combo_box);
  return TRUE;
}

/**
 * combo_box_key_press
 * @widget:     Widget
 * @event:      Event
 * @combo_box:  Combo box
 *
 * Key press handler which dismisses popup on escape.
 * Popup is dismissed whether or not popup is torn off.
 */
static gint
combo_box_key_press (GtkWidget * widget, GdkEventKey * event,
		     ComboBox * combo_box)
{
  if (event->keyval == GDK_Escape)
    {
      combo_box_popup_hide_unconditional (combo_box);
      return TRUE;
    }
  else
    return FALSE;
}

static void
cb_state_change (GtkWidget * widget, GtkStateType old_state,
		 ComboBox * combo_box)
{
  GtkStateType const new_state = GTK_WIDGET_STATE (widget);
  gtk_widget_set_state (combo_box->priv->display_widget, new_state);
}

static void
combo_box_init (ComboBox * combo_box)
{
  GtkWidget *arrow;
  GdkCursor *cursor;

  combo_box->priv = g_new0 (ComboBoxPrivate, 1);

  /*
   * Create the arrow
   */
  combo_box->priv->arrow_button = gtk_toggle_button_new ();
  GTK_WIDGET_UNSET_FLAGS (combo_box->priv->arrow_button, GTK_CAN_FOCUS);

  arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (combo_box->priv->arrow_button), arrow);
  gtk_box_pack_end (GTK_BOX (combo_box), combo_box->priv->arrow_button, FALSE,
		    FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (combo_box->priv->arrow_button), "toggled",
		      GTK_SIGNAL_FUNC (combo_toggle_pressed), combo_box);
  gtk_widget_show_all (combo_box->priv->arrow_button);

  /*
   * prelight the display widget when mousing over the arrow.
   */
  gtk_signal_connect (GTK_OBJECT (combo_box->priv->arrow_button),
		      "state-changed", GTK_SIGNAL_FUNC (cb_state_change),
		      combo_box);

  /*
   * The pop-down container
   */

  combo_box->priv->toplevel = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_widget_ref (combo_box->priv->toplevel);
  gtk_object_sink (GTK_OBJECT (combo_box->priv->toplevel));
  gtk_window_set_policy (GTK_WINDOW (combo_box->priv->toplevel),
			 FALSE, TRUE, FALSE);

  combo_box->priv->popup = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER (combo_box->priv->toplevel),
		     combo_box->priv->popup);
  gtk_widget_show (combo_box->priv->popup);

  gtk_widget_realize (combo_box->priv->popup);
  cursor = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
  gdk_window_set_cursor (combo_box->priv->popup->window, cursor);
  gdk_cursor_destroy (cursor);

  combo_box->priv->torn_off = FALSE;
  combo_box->priv->tearoff_window = NULL;

  combo_box->priv->frame = gtk_frame_new (NULL);
  gtk_container_add (GTK_CONTAINER (combo_box->priv->popup),
		     combo_box->priv->frame);
  gtk_frame_set_shadow_type (GTK_FRAME (combo_box->priv->frame),
			     GTK_SHADOW_OUT);

  gtk_signal_connect (GTK_OBJECT (combo_box->priv->toplevel),
		      "button_press_event",
		      GTK_SIGNAL_FUNC (combo_box_button_press), combo_box);
  gtk_signal_connect (GTK_OBJECT (combo_box->priv->toplevel),
		      "key_press_event",
		      GTK_SIGNAL_FUNC (combo_box_key_press), combo_box);
}

GType
combo_box_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      GTypeInfo info = {
	sizeof (ComboBoxClass),
	NULL,
	NULL,
	(GClassInitFunc) combo_box_class_init,
	NULL,
	NULL,
	sizeof (ComboBox),
	0,
	(GInstanceInitFunc) combo_box_init,
      };

      type = g_type_register_static (gtk_hbox_get_type (), "MyComboBox",
				     &info, 0);
    }

  return type;
}

/**
 * combo_box_set_display:
 * @combo_box: the Combo Box to modify
 * @display_widget: The widget to be displayed

 * Sets the displayed widget for the @combo_box to be @display_widget
 */
void
combo_box_set_display (ComboBox * combo_box, GtkWidget * display_widget)
{
  g_return_if_fail (combo_box != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo_box));
  g_return_if_fail (display_widget != NULL);
  g_return_if_fail (GTK_IS_WIDGET (display_widget));

  if (combo_box->priv->display_widget &&
      combo_box->priv->display_widget != display_widget)
    gtk_container_remove (GTK_CONTAINER (combo_box),
			  combo_box->priv->display_widget);

  combo_box->priv->display_widget = display_widget;

  gtk_box_pack_start (GTK_BOX (combo_box), display_widget, TRUE, TRUE, 0);
}

static gboolean
cb_tearable_enter_leave (GtkWidget * w, GdkEventCrossing * event,
			 gpointer data)
{
  gboolean const flag = GPOINTER_TO_INT (data);
  gtk_widget_set_state (w, flag ? GTK_STATE_PRELIGHT : GTK_STATE_NORMAL);
  return FALSE;
}

/**
 * combo_popup_tear_off
 * @combo:         Combo box
 * @set_position:  Set to position of popup shell if true
 *
 * Tear off the popup
 *
 * FIXME:
 * Gtk popup menus are toplevel windows, not dialogs. I think this is wrong,
 * and make the popups dialogs. But may be there should be a way to make
 * them toplevel. We can do this after creating:
 * GTK_WINDOW (tearoff)->type = GTK_WINDOW_TOPLEVEL;
 */
static void
combo_popup_tear_off (ComboBox * combo, gboolean set_position)
{
  int x, y;

  if (!combo->priv->tearoff_window)
    {
      GtkWidget *tearoff;
      gchar *title;

      tearoff = gtk_window_new (GTK_WINDOW_POPUP);
      gtk_widget_ref (tearoff);
      gtk_object_sink (GTK_OBJECT (tearoff));
      combo->priv->tearoff_window = tearoff;
      gtk_widget_set_app_paintable (tearoff, TRUE);
      gtk_signal_connect (GTK_OBJECT (tearoff), "key_press_event",
			  GTK_SIGNAL_FUNC (combo_box_key_press),
			  GTK_OBJECT (combo));
      gtk_widget_realize (tearoff);
      title = gtk_object_get_data (GTK_OBJECT (combo), "combo-title");
      if (title)
	gdk_window_set_title (tearoff->window, title);
      gtk_window_set_policy (GTK_WINDOW (tearoff), FALSE, TRUE, FALSE);
      gtk_window_set_transient_for
	(GTK_WINDOW (tearoff),
	 GTK_WINDOW (gtk_widget_get_toplevel GTK_WIDGET (combo)));
    }

  if (GTK_WIDGET_VISIBLE (combo->priv->popup))
    {
      gtk_widget_hide (combo->priv->toplevel);

      gtk_grab_remove (combo->priv->toplevel);
      gdk_pointer_ungrab (GDK_CURRENT_TIME);
    }

  combo_popup_reparent (combo->priv->popup,
			combo->priv->tearoff_window, FALSE);

  /* It may have got confused about size */
  gtk_widget_queue_resize (GTK_WIDGET (combo->priv->popup));

  if (set_position)
    {
      combo_box_get_pos (combo, &x, &y);
      gtk_widget_set_uposition (combo->priv->tearoff_window, x, y);
    }
  gtk_widget_show (GTK_WIDGET (combo->priv->popup));
  gtk_widget_show (combo->priv->tearoff_window);

}

/**
 * combo_set_tearoff_state
 * @combo_box:  Combo box
 * @torn_off:   TRUE: Tear off. FALSE: Pop down and reattach
 *
 * Set the tearoff state of the popup
 *
 * Compare with gtk_menu_set_tearoff_state in gtk/gtkmenu.c
 */
static void
combo_set_tearoff_state (ComboBox * combo, gboolean torn_off)
{
  g_return_if_fail (combo != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo));

  if (combo->priv->torn_off != torn_off)
    {
      combo->priv->torn_off = torn_off;

      if (combo->priv->torn_off)
	{
	  combo_popup_tear_off (combo, TRUE);
	  deactivate_arrow (combo);
	}
      else
	{
	  gtk_widget_hide (combo->priv->tearoff_window);
	  combo_popup_reparent (combo->priv->popup,
				combo->priv->toplevel, FALSE);
	}
    }
}

/**
 * combo_tearoff_bg_copy
 * @combo_box:  Combo box
 *
 * Copy popup window image to the tearoff window.
 */
static void
combo_tearoff_bg_copy (ComboBox * combo)
{
  GdkPixmap *pixmap;
  GdkGC *gc;
  GdkGCValues gc_values;

  GtkWidget *widget = combo->priv->popup;

  if (combo->priv->torn_off)
    {
      gc_values.subwindow_mode = GDK_INCLUDE_INFERIORS;
      gc = gdk_gc_new_with_values (widget->window,
				   &gc_values, GDK_GC_SUBWINDOW);

      pixmap = gdk_pixmap_new (widget->window,
			       widget->allocation.width,
			       widget->allocation.height, -1);

      gdk_draw_pixmap (pixmap, gc, widget->window, 0, 0, 0, 0, -1, -1);
      gdk_gc_unref (gc);

      gtk_widget_set_usize (combo->priv->tearoff_window,
			    widget->allocation.width,
			    widget->allocation.height);

      gdk_window_set_back_pixmap
	(combo->priv->tearoff_window->window, pixmap, FALSE);
      gdk_pixmap_unref (pixmap);
    }
}

/**
 * combo_popup_reparent
 * @popup:       Popup
 * @new_parent:  New parent
 * @unrealize:   Unrealize popup if TRUE.
 *
 * Reparent the popup, taking care of the refcounting
 *
 * Compare with gtk_menu_reparent in gtk/gtkmenu.c
 */
static void
combo_popup_reparent (GtkWidget * popup,
		      GtkWidget * new_parent, gboolean unrealize)
{
  GtkObject *object = GTK_OBJECT (popup);
  gboolean was_floating = GTK_OBJECT_FLOATING (object);

  g_object_ref (G_OBJECT (object));
  gtk_object_sink (object);

  if (unrealize)
    {
      g_object_ref (G_OBJECT (object));
      gtk_container_remove (GTK_CONTAINER (popup->parent), popup);
      gtk_container_add (GTK_CONTAINER (new_parent), popup);
      g_object_unref (G_OBJECT (object));
    }
  else
    gtk_widget_reparent (GTK_WIDGET (popup), new_parent);
  gtk_widget_set_usize (new_parent, -1, -1);

  if (was_floating)
    GTK_OBJECT_SET_FLAGS (object, GTK_FLOATING);
  else
    g_object_unref (G_OBJECT (object));
}

/**
 * cb_tearable_button_release
 * @w:      Widget
 * @event:  Event
 * @combo:  Combo box
 *
 * Toggle tearoff state.
 */
static gboolean
cb_tearable_button_release (GtkWidget * w, GdkEventButton * event,
			    ComboBox * combo)
{
  GtkTearoffMenuItem *tearable;

  g_return_val_if_fail (w != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_TEAROFF_MENU_ITEM (w), FALSE);

  tearable = GTK_TEAROFF_MENU_ITEM (w);
  tearable->torn_off = !tearable->torn_off;

  if (!combo->priv->torn_off)
    {
      gboolean need_connect;

      need_connect = (!combo->priv->tearoff_window);
      combo_set_tearoff_state (combo, TRUE);
      if (need_connect)
	gtk_signal_connect
	  (GTK_OBJECT
	   (combo->priv->tearoff_window),
	   "delete_event", GTK_SIGNAL_FUNC (cb_popup_delete), combo);
    }
  else
    combo_box_popup_hide_unconditional (combo);

  return TRUE;
}

static gboolean
cb_popup_delete (GtkWidget * w, GdkEventAny * event, ComboBox * combo)
{
  combo_box_popup_hide_unconditional (combo);
  return TRUE;
}

void
combo_box_construct (ComboBox * combo_box, GtkWidget * display_widget,
		     GtkWidget * pop_down_widget)
{
  GtkWidget *tearable;
  GtkWidget *vbox;

  g_return_if_fail (combo_box != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo_box));
  g_return_if_fail (display_widget != NULL);
  g_return_if_fail (GTK_IS_WIDGET (display_widget));

  GTK_BOX (combo_box)->spacing = 0;
  GTK_BOX (combo_box)->homogeneous = FALSE;

  combo_box->priv->pop_down_widget = pop_down_widget;
  combo_box->priv->display_widget = NULL;

  vbox = gtk_vbox_new (FALSE, 5);
  tearable = gtk_tearoff_menu_item_new ();
  gtk_signal_connect (GTK_OBJECT (tearable), "enter-notify-event",
		      GTK_SIGNAL_FUNC (cb_tearable_enter_leave),
		      GINT_TO_POINTER (TRUE));
  gtk_signal_connect (GTK_OBJECT (tearable), "leave-notify-event",
		      GTK_SIGNAL_FUNC (cb_tearable_enter_leave),
		      GINT_TO_POINTER (FALSE));
  gtk_signal_connect (GTK_OBJECT (tearable), "button-release-event",
		      GTK_SIGNAL_FUNC (cb_tearable_button_release),
		      (gpointer) combo_box);
  gtk_box_pack_start (GTK_BOX (vbox), tearable, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), pop_down_widget, TRUE, TRUE, 0);
  combo_box->priv->tearable = tearable;

  /*
   * Finish setup
   */
  combo_box_set_display (combo_box, display_widget);

  gtk_container_add (GTK_CONTAINER (combo_box->priv->frame), vbox);
  gtk_widget_show_all (combo_box->priv->frame);
}

GtkWidget *
combo_box_new (GtkWidget * display_widget, GtkWidget * optional_popdown)
{
  ComboBox *combo_box;

  g_return_val_if_fail (display_widget != NULL, NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (display_widget), NULL);

  combo_box = gtk_type_new (combo_box_get_type ());
  combo_box_construct (combo_box, display_widget, optional_popdown);
  return GTK_WIDGET (combo_box);
}

void
combo_box_set_arrow_relief (ComboBox * cc, GtkReliefStyle relief)
{
  g_return_if_fail (cc != NULL);
  g_return_if_fail (IS_COMBO_BOX (cc));

  gtk_button_set_relief (GTK_BUTTON (cc->priv->arrow_button), relief);
}

/**
 * combo_box_set_title
 * @combo: Combo box
 * @title: Title
 *
 * Set a title to display over the tearoff window.
 *
 * FIXME:
 *
 * This should really change the title even when the popup is already torn off.
 * I guess the tearoff window could attach a listener to title change or
 * something. But I don't think we need the functionality, so I didn't bother
 * to investigate.
 */
void
combo_box_set_title (ComboBox * combo, const gchar * title)
{
  g_return_if_fail (combo != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo));

  gtk_object_set_data_full (GTK_OBJECT (combo), "combo-title",
			    g_strdup (title), (GtkDestroyNotify) g_free);
}

/**
 * combo_box_set_arrow_sensitive
 * @combo:  Combo box
 * @sensitive:  Sensitivity value
 *
 * Toggle the sensitivity of the arrow button
 */

void
combo_box_set_arrow_sensitive (ComboBox * combo, gboolean sensitive)
{
  g_return_if_fail (combo != NULL);

  gtk_widget_set_sensitive (combo->priv->arrow_button, sensitive);
}

/**
 * combo_box_set_tearable:
 * @combo: Combo box
 * @tearable: whether to allow the @combo to be tearable
 *
 * controls whether the combo box's pop up widget can be torn off.
 */
void
combo_box_set_tearable (ComboBox * combo, gboolean tearable)
{
  g_return_if_fail (combo != NULL);
  g_return_if_fail (IS_COMBO_BOX (combo));

  if (tearable)
    {
      gtk_widget_show (combo->priv->tearable);
    }
  else
    {
      combo_set_tearoff_state (combo, FALSE);
      gtk_widget_hide (combo->priv->tearable);
    }
}
