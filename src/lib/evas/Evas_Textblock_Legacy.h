#ifndef _EVAS_H
# error You shall not include this header directly
#endif

typedef struct _Evas_Textblock_Node_Format Evas_Textblock_Node_Format;

/**
 * @brief Remove a format node and its match.
 *
 * @param[in] n
 *
 * @ingroup Evas_Textblock
 */
EAPI void evas_textblock_node_format_remove_pair(Evas_Object *obj, Evas_Textblock_Node_Format *n);

EAPI const Eina_List *evas_textblock_node_format_list_get(const Evas_Object *obj, const char *anchor);

/** Gets the first format node.
 *
 * @ingroup Evas_Textblock
 */
EAPI const Evas_Textblock_Node_Format *evas_textblock_node_format_first_get(const Evas_Object *obj);

/** Gets the last format node.
 *
 * @ingroup Evas_Textblock
 */
EAPI const Evas_Textblock_Node_Format *evas_textblock_node_format_last_get(const Evas_Object *obj);

/* ========================== Cursor API =================================== */
/**
 * @typedef Evas_Textblock_Cursor
 *
 * A textblock cursor object, used to maipulate the cursor of an evas textblock
 * @see evas_object_textblock_cursor_new
 *
 */
typedef struct _Evas_Textblock_Cursor             Evas_Textblock_Cursor;

/**
 * Cursor type for evas textblock.
 */
typedef enum _Evas_Textblock_Cursor_Type
{
   EVAS_TEXTBLOCK_CURSOR_UNDER, /**< cursor type is under */
   EVAS_TEXTBLOCK_CURSOR_BEFORE /**< cursor type is before */
} Evas_Textblock_Cursor_Type;


/**
 * Free the cursor and unassociate it from the object.
 * @note do not use it to free unassociated cursors.
 *
 * @param cur the cursor to free.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_free(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Sets the cursor to the start of the first text node.
 *
 * @param cur the cursor to update.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_paragraph_first(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * sets the cursor to the end of the last text node.
 *
 * @param cur the cursor to set.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_paragraph_last(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Advances to the start of the next text node
 *
 * @param cur the cursor to update
 * @return @c EINA_TRUE if it managed to advance a paragraph, @c EINA_FALSE
 * otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_paragraph_next(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Advances to the end of the previous text node
 *
 * @param cur the cursor to update
 * @return @c EINA_TRUE if it managed to advance a paragraph, @c EINA_FALSE
 * otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_paragraph_prev(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Sets the cursor to point to the place where format points to.
 *
 * @param cur the cursor to update.
 * @param n the format node to update according.
 * @deprecated duplicate of evas_textblock_cursor_at_format_set
 */
EAPI void                                     evas_textblock_cursor_set_at_format(Evas_Textblock_Cursor *cur, const Evas_Object_Textblock_Node_Format *n) EINA_ARG_NONNULL(1, 2);

/**
 * Return the format node at the position pointed by cur.
 *
 * @param cur the position to look at.
 * @return the format node if found, @c NULL otherwise.
 * @see evas_textblock_cursor_format_is_visible_get()
 */
EAPI const Evas_Object_Textblock_Node_Format *evas_textblock_cursor_format_get(const Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Set the cursor to point to the position of fmt.
 *
 * @param cur the cursor to update
 * @param fmt the format to update according to.
 */
EAPI void                                     evas_textblock_cursor_at_format_set(Evas_Textblock_Cursor *cur, const Evas_Object_Textblock_Node_Format *fmt) EINA_ARG_NONNULL(1, 2);

/**
 * Check if the current cursor position is a visible format. This way is more
 * efficient than evas_textblock_cursor_format_get() to check for the existence
 * of a visible format.
 *
 * @param cur the cursor to look at.
 * @return @c EINA_TRUE if the cursor points to a visible format, @c EINA_FALSE
 * otherwise.
 * @see evas_textblock_cursor_format_get()
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_is_visible_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1);

/**
 * Advances to the next format node
 *
 * @param cur the cursor to be updated.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_next(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Advances to the previous format node.
 *
 * @param cur the cursor to update.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_prev(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Returns true if the cursor points to a format.
 *
 * @param cur the cursor to check.
 * @return  @c EINA_TRUE if a cursor points to a format @c EINA_FALSE
 * otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_is_format(const Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);


/**
 * Advances 1 char forward.
 *
 * @param cur the cursor to advance.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_char_next(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Advances 1 char backward.
 *
 * @param cur the cursor to advance.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_char_prev(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Moves the cursor to the start of the word under the cursor.
 *
 * @param cur the cursor to move.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 * @since 1.2
 */
EAPI Eina_Bool                                evas_textblock_cursor_word_start(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Moves the cursor to the end of the word under the cursor.
 *
 * @param cur the cursor to move.
 * @return @c EINA_TRUE on success @c EINA_FALSE otherwise.
 * @since 1.2
 */
EAPI Eina_Bool                                evas_textblock_cursor_word_end(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Go to the first char in the node the cursor is pointing on.
 *
 * @param cur the cursor to update.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_paragraph_char_first(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Go to the last char in a text node.
 *
 * @param cur the cursor to update.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_paragraph_char_last(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Go to the start of the current line
 *
 * @param cur the cursor to update.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_line_char_first(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Go to the end of the current line.
 *
 * @param cur the cursor to update.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_line_char_last(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Return the current cursor pos.
 *
 * @param cur the cursor to take the position from.
 * @return the position or -1 on error
 */
EAPI int                                      evas_textblock_cursor_pos_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1);

/**
 * Set the cursor pos.
 *
 * @param cur the cursor to be set.
 * @param pos the pos to set.
 */
EAPI void                                     evas_textblock_cursor_pos_set(Evas_Textblock_Cursor *cur, int pos) EINA_ARG_NONNULL(1);

/**
 * Go to the start of the line passed
 *
 * @param cur cursor to update.
 * @param line numer to set.
 * @return @c EINA_TRUE on success, @c EINA_FALSE on error.
 */
EAPI Eina_Bool                                evas_textblock_cursor_line_set(Evas_Textblock_Cursor *cur, int line) EINA_ARG_NONNULL(1);

/**
 * Compare two cursors.
 *
 * @param cur1 the first cursor.
 * @param cur2 the second cursor.
 * @return -1 if cur1 < cur2, 0 if cur1 == cur2 and 1 otherwise.
 */
EAPI int                                      evas_textblock_cursor_compare(const Evas_Textblock_Cursor *cur1, const Evas_Textblock_Cursor *cur2) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1, 2);

/**
 * Make cur_dest point to the same place as cur. Does not work if they don't
 * point to the same object.
 *
 * @param cur the source cursor.
 * @param cur_dest destination cursor.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_copy(const Evas_Textblock_Cursor *cur, Evas_Textblock_Cursor *cur_dest) EINA_ARG_NONNULL(1, 2);

/**
 * Adds text to the current cursor position and set the cursor to *before*
 * the start of the text just added.
 *
 * @param cur the cursor to where to add text at.
 * @param text the text to add.
 * @return Returns the len of the text added.
 * @see evas_textblock_cursor_text_prepend()
 */
EAPI int                                      evas_textblock_cursor_text_append(Evas_Textblock_Cursor *cur, const char *text) EINA_ARG_NONNULL(1, 2);

/**
 * Adds text to the current cursor position and set the cursor to *after*
 * the start of the text just added.
 *
 * @param cur the cursor to where to add text at.
 * @param text the text to add.
 * @return Returns the len of the text added.
 * @see evas_textblock_cursor_text_append()
 */
EAPI int                                      evas_textblock_cursor_text_prepend(Evas_Textblock_Cursor *cur, const char *text) EINA_ARG_NONNULL(1, 2);

/**
 * Adds format to the current cursor position. If the format being added is a
 * visible format, add it *before* the cursor position, otherwise, add it after.
 * This behavior is because visible formats are like characters and invisible
 * should be stacked in a way that the last one is added last.
 *
 * This function works with native formats, that means that style defined
 * tags like <br> won't work here. For those kind of things use markup prepend.
 *
 * @param cur the cursor to where to add format at.
 * @param format the format to add.
 * @return Returns true if a visible format was added, false otherwise.
 * @see evas_textblock_cursor_format_prepend()
 */

/**
 * Check if the current cursor position points to the terminating null of the
 * last paragraph. (shouldn't be allowed to point to the terminating null of
 * any previous paragraph anyway.
 *
 * @param cur the cursor to look at.
 * @return @c EINA_TRUE if the cursor points to the terminating null, @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_append(Evas_Textblock_Cursor *cur, const char *format) EINA_ARG_NONNULL(1, 2);

/**
 * Adds format to the current cursor position. If the format being added is a
 * visible format, add it *before* the cursor position, otherwise, add it after.
 * This behavior is because visible formats are like characters and invisible
 * should be stacked in a way that the last one is added last.
 * If the format is visible the cursor is advanced after it.
 *
 * This function works with native formats, that means that style defined
 * tags like <br> won't work here. For those kind of things use markup prepend.
 *
 * @param cur the cursor to where to add format at.
 * @param format the format to add.
 * @return Returns true if a visible format was added, false otherwise.
 * @see evas_textblock_cursor_format_prepend()
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_prepend(Evas_Textblock_Cursor *cur, const char *format) EINA_ARG_NONNULL(1, 2);

/**
 * Delete the character at the location of the cursor. If there's a format
 * pointing to this position, delete it as well.
 *
 * @param cur the cursor pointing to the current location.
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_char_delete(Evas_Textblock_Cursor *cur) EINA_ARG_NONNULL(1);

/**
 * Delete the range between cur1 and cur2.
 *
 * @param cur1 one side of the range.
 * @param cur2 the second side of the range
 * @return Returns no value.
 */
EAPI void                                     evas_textblock_cursor_range_delete(Evas_Textblock_Cursor *cur1, Evas_Textblock_Cursor *cur2) EINA_ARG_NONNULL(1, 2);

/**
 * Return the text of the paragraph cur points to - returns the text in markup.
 *
 * @param cur the cursor pointing to the paragraph.
 * @return the text on success, @c NULL otherwise.
 */
EAPI const char                              *evas_textblock_cursor_paragraph_text_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1);

/**
 * Return the length of the paragraph, cheaper the eina_unicode_strlen()
 *
 * @param cur the position of the paragraph.
 * @return the length of the paragraph on success, -1 otehrwise.
 */
EAPI int                                      evas_textblock_cursor_paragraph_text_length_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1);

/**
 * Return the currently visible range.
 *
 * @param start the start of the range.
 * @param end the end of the range.
 * @return @c EINA_TRUE on success, @c EINA_FALSE otherwise.
 * @since 1.1
 */
EAPI Eina_Bool                                evas_textblock_cursor_visible_range_get(Evas_Textblock_Cursor *start, Evas_Textblock_Cursor *end) EINA_ARG_NONNULL(1, 2);

/**
 * Return the format nodes in the range between cur1 and cur2.
 *
 * @param cur1 one side of the range.
 * @param cur2 the other side of the range
 * @return the foramt nodes in the range. You have to free it.
 * @since 1.1
 */
EAPI Eina_List                               *evas_textblock_cursor_range_formats_get(const Evas_Textblock_Cursor *cur1, const Evas_Textblock_Cursor *cur2) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1, 2);

/**
 * Return the text in the range between cur1 and cur2
 *
 * @param cur1 one side of the range.
 * @param cur2 the other side of the range
 * @param format The form on which to return the text. Markup - in textblock markup. Plain - UTF8.
 * @return the text in the range
 * @see elm_entry_markup_to_utf8()
 */
EAPI char                                    *evas_textblock_cursor_range_text_get(const Evas_Textblock_Cursor *cur1, const Evas_Textblock_Cursor *cur2, Evas_Textblock_Text_Type format) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1, 2);

/**
 * Return the content of the cursor.
 *
 * Free the returned string pointer when done (if it is not NULL).
 *
 * @param cur the cursor
 * @return the text in the range, terminated by a nul byte (may be utf8).
 */
EAPI char                                    *evas_textblock_cursor_content_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1) EINA_MALLOC;

/**
 * Returns the geometry of two cursors ("split cursor"), if logical cursor is
 * between LTR/RTL text, also considering paragraph direction.
 * Upper cursor is shown for the text of the same direction as paragraph,
 * lower cursor - for opposite.
 *
 * Split cursor geometry is valid only  in '|' cursor mode.
 * In this case @c EINA_TRUE is returned and cx2, cy2, cw2, ch2 are set,
 * otherwise it behaves like cursor_geometry_get.
 *
 * @param[in] cur the cursor.
 * @param[out] cx the x of the cursor (or upper cursor)
 * @param[out] cy the y of the cursor (or upper cursor)
 * @param[out] cw the width of the cursor (or upper cursor)
 * @param[out] ch the height of the cursor (or upper cursor)
 * @param[out] cx2 the x of the lower cursor
 * @param[out] cy2 the y of the lower cursor
 * @param[out] cw2 the width of the lower cursor
 * @param[out] ch2 the height of the lower cursor
 * @param[in] ctype the type of the cursor.
 * @return @c EINA_TRUE for split cursor, @c EINA_FALSE otherwise
 * @since 1.8
 */
EAPI Eina_Bool
evas_textblock_cursor_geometry_bidi_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cx, Evas_Coord *cy, Evas_Coord *cw, Evas_Coord *ch, Evas_Coord *cx2, Evas_Coord *cy2, Evas_Coord *cw2, Evas_Coord *ch2, Evas_Textblock_Cursor_Type ctype);

/**
 * Returns the geometry of the cursor. Depends on the type of cursor requested.
 * This should be used instead of char_geometry_get because there are weird
 * special cases with BiDi text.
 * in '_' cursor mode (i.e a line below the char) it's the same as char_geometry
 * get, except for the case of the last char of a line which depends on the
 * paragraph direction.
 *
 * in '|' cursor mode (i.e a line between two chars) it is very variable.
 * For example consider the following visual string:
 * "abcCBA" (ABC are rtl chars), a cursor pointing on A should actually draw
 * a '|' between the c and the C.
 *
 * @param cur the cursor.
 * @param cx the x of the cursor
 * @param cy the y of the cursor
 * @param cw the width of the cursor
 * @param ch the height of the cursor
 * @param dir the direction of the cursor, can be NULL.
 * @param ctype the type of the cursor.
 * @return line number of the char on success, -1 on error.
 */
EAPI int                                      evas_textblock_cursor_geometry_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cx, Evas_Coord *cy, Evas_Coord *cw, Evas_Coord *ch, Evas_BiDi_Direction *dir, Evas_Textblock_Cursor_Type ctype) EINA_ARG_NONNULL(1);

/**
 * Returns the geometry of the char at cur.
 *
 * @param cur the position of the char.
 * @param cx the x of the char.
 * @param cy the y of the char.
 * @param cw the w of the char.
 * @param ch the h of the char.
 * @return line number of the char on success, -1 on error.
 */
EAPI int                                      evas_textblock_cursor_char_geometry_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cx, Evas_Coord *cy, Evas_Coord *cw, Evas_Coord *ch) EINA_ARG_NONNULL(1);

/**
 * Returns the geometry of the pen at cur.
 *
 * @param cur the position of the char.
 * @param cpen_x the pen_x of the char.
 * @param cy the y of the char.
 * @param cadv the adv of the char.
 * @param ch the h of the char.
 * @return line number of the char on success, -1 on error.
 */
EAPI int                                      evas_textblock_cursor_pen_geometry_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cpen_x, Evas_Coord *cy, Evas_Coord *cadv, Evas_Coord *ch) EINA_ARG_NONNULL(1);

/**
 * Returns the geometry of the line at cur.
 *
 * @param cur the position of the line.
 * @param cx the x of the line.
 * @param cy the y of the line.
 * @param cw the width of the line.
 * @param ch the height of the line.
 * @return line number of the line on success, -1 on error.
 */
EAPI int                                      evas_textblock_cursor_line_geometry_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cx, Evas_Coord *cy, Evas_Coord *cw, Evas_Coord *ch) EINA_ARG_NONNULL(1);

/**
 * Set the position of the cursor according to the X and Y coordinates.
 *
 * @param cur the cursor to set.
 * @param x coord to set by.
 * @param y coord to set by.
 * @return @c EINA_TRUE on success, @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_char_coord_set(Evas_Textblock_Cursor *cur, Evas_Coord x, Evas_Coord y) EINA_ARG_NONNULL(1);

/**
 * Set the cursor position according to the y coord.
 *
 * @param cur the cur to be set.
 * @param y the coord to set by.
 * @return the line number found, -1 on error.
 */
EAPI int                                      evas_textblock_cursor_line_coord_set(Evas_Textblock_Cursor *cur, Evas_Coord y) EINA_ARG_NONNULL(1);

/**
 * Get the geometry of a range.
 *
 * @param cur1 one side of the range.
 * @param cur2 other side of the range.
 * @return a list of Rectangles representing the geometry of the range.
 */
EAPI Eina_List                               *evas_textblock_cursor_range_geometry_get(const Evas_Textblock_Cursor *cur1, const Evas_Textblock_Cursor *cur2) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1, 2);

/**
 * Get the simple geometry of a range.
 * The simple geometry is the geomtry in which rectangles in middle
 * lines of range are merged into one big rectangle.
 * @since 1.13
 *
 * @param cur1 one side of the range.
 * @param cur2 other side of the range.
 * @return an iterator of rectangles representing the geometry of the range.
 */
EAPI Eina_Iterator                               *evas_textblock_cursor_range_simple_geometry_get(const Evas_Textblock_Cursor *cur1, const Evas_Textblock_Cursor *cur2) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1, 2);

/**
 * Get the geometry of ?
 *
 * @param cur one side of the range.
 * @param cur2 other side of the range.
 */
EAPI Eina_Bool                                evas_textblock_cursor_format_item_geometry_get(const Evas_Textblock_Cursor *cur, Evas_Coord *cx, Evas_Coord *cy, Evas_Coord *cw, Evas_Coord *ch) EINA_ARG_NONNULL(1);

/**
 * Checks if the cursor points to the end of the line.
 *
 * @param cur the cursor to check.
 * @return @c EINA_TRUE if true, @c EINA_FALSE otherwise.
 */
EAPI Eina_Bool                                evas_textblock_cursor_eol_get(const Evas_Textblock_Cursor *cur) EINA_WARN_UNUSED_RESULT EINA_ARG_NONNULL(1);

/**
 * Prepends markup to the cursor cur.
 *
 * @note assumes text does not include the unicode object replacement char (0xFFFC)
 *
 * @param cur  the cursor to prepend to.
 * @param text the markup text to prepend.
 * @return Return no value.
 */
EAPI void                                     evas_object_textblock_text_markup_prepend(Evas_Textblock_Cursor *cur, const char *text) EINA_ARG_NONNULL(1, 2);

