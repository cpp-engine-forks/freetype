/***************************************************************************/
/*                                                                         */
/*  hbshim.c                                                               */
/*                                                                         */
/*    HarfBuzz interface for accessing OpenType features (body).           */
/*                                                                         */
/*  Copyright 2013 by                                                      */
/*  David Turner, Robert Wilhelm, and Werner Lemberg.                      */
/*                                                                         */
/*  This file is part of the FreeType project, and may only be used,       */
/*  modified, and distributed under the terms of the FreeType project      */
/*  license, LICENSE.TXT.  By continuing to use, modify, or distribute     */
/*  this file you indicate that you have read the license and              */
/*  understand and accept it fully.                                        */
/*                                                                         */
/***************************************************************************/


#include <ft2build.h>
#include FT_FREETYPE_H


#ifdef FT_CONFIG_OPTION_USE_HARFBUZZ

#include <hb.h>
#include <hb-ot.h>
#include <hb-ft.h>


  /*************************************************************************/
  /*                                                                       */
  /* The macro FT_COMPONENT is used in trace mode.  It is an implicit      */
  /* parameter of the FT_TRACE() and FT_ERROR() macros, used to print/log  */
  /* messages during execution.                                            */
  /*                                                                       */
#undef  FT_COMPONENT
#define FT_COMPONENT  trace_afharfbuzz


  /*
   * We use `sets' (in the HarfBuzz sense, which comes quite near to the
   * usual mathematical meaning) to manage both lookups and glyph indices.
   *
   * 1. For each coverage, collect lookup IDs in a set.  Note that an
   *    auto-hinter `coverage' is represented by one or more `feature's, and
   *    a feature consists of an arbitrary number of (font specific)
   *    `lookup's that actually do the mapping job.  Please check the
   *    OpenType specification for more details on features and lookups.
   *
   * 2. Create glyph ID sets from the corresponding lookup sets.
   *
   * 3. The glyph set corresponding to AF_COVERAGE_DEFAULT is computed
   *    with all lookups specific to the OpenType script activated.  It
   *    relies on the order of AF_DEFINE_STYLE_CLASS entries so that
   *    special coverages (like `oldstyle figures') don't get overwritten.
   *
   */


  /* load coverage tags */
#undef  COVERAGE_1
#define COVERAGE_1( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4 )   \
          static const hb_tag_t  name ## _coverage[] = \
          {                                            \
            HB_TAG( tag_a1, tag_a2, tag_a3, tag_a4 ),  \
            HB_TAG_NONE                                \
          };

#undef  COVERAGE_2
#define COVERAGE_2( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4,    \
                    tag_b1, tag_b2, tag_b3, tag_b4 )   \
          static const hb_tag_t  name ## _coverage[] = \
          {                                            \
            HB_TAG( tag_a1, tag_a2, tag_a3, tag_a4 ),  \
            HB_TAG( tag_b1, tag_b2, tag_b3, tag_b4 ),  \
            HB_TAG_NONE                                \
          };

#undef  COVERAGE_3
#define COVERAGE_3( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4,    \
                    tag_b1, tag_b2, tag_b3, tag_b4,    \
                    tag_c1, tag_c2, tag_c3, tag_c4 )   \
          static const hb_tag_t  name ## _coverage[] = \
          {                                            \
            HB_TAG( tag_a1, tag_a2, tag_a3, tag_a4 ),  \
            HB_TAG( tag_b1, tag_b2, tag_b3, tag_b4 ),  \
            HB_TAG( tag_c1, tag_c2, tag_c3, tag_c4 ),  \
            HB_TAG_NONE                                \
          };


#include "afcover.h"


  /* define mapping between coverage tags and AF_Coverage */
#undef  COVERAGE_1
#define COVERAGE_1( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4 )   \
          name ## _coverage,

#undef  COVERAGE_2
#define COVERAGE_2( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4,    \
                    tag_b1, tag_b2, tag_b3, tag_b4 )   \
          name ## _coverage,

#undef  COVERAGE_3
#define COVERAGE_3( name, NAME, description,           \
                    tag_a1, tag_a2, tag_a3, tag_a4,    \
                    tag_b1, tag_b2, tag_b3, tag_b4,    \
                    tag_c1, tag_c2, tag_c3, tag_c4 )   \
          name ## _coverage,


  static const hb_tag_t*  coverages[] =
  {
#include "afcover.h"

    NULL /* AF_COVERAGE_DEFAULT */
  };


  /* load HarfBuzz script tags */
#undef  SCRIPT
#define SCRIPT( s, S, d, h, dc )  h,


  static const hb_tag_t  scripts[] =
  {
#include "afscript.h"
  };


  FT_Error
  af_get_coverage( AF_FaceGlobals  globals,
                   AF_StyleClass   style_class,
                   FT_Byte*        gstyles )
  {
    hb_face_t*  face;

    hb_set_t*  lookups;  /* lookups for a given script */
    hb_set_t*  glyphs;   /* glyphs covered by lookups  */

    hb_tag_t         script;
    const hb_tag_t*  coverage_tags;
    hb_tag_t         script_tags[] = { HB_TAG_NONE,
                                       HB_TAG_NONE,
                                       HB_TAG_NONE,
                                       HB_TAG_NONE };

    hb_codepoint_t  idx;
#ifdef FT_DEBUG_LEVEL_TRACE
    int             count;
#endif


    if ( !globals || !style_class || !gstyles )
      return FT_THROW( Invalid_Argument );

    face = hb_ft_face_create( globals->face, NULL );

    lookups = hb_set_create();
    glyphs  = hb_set_create();

    coverage_tags = coverages[style_class->coverage];
    script        = scripts[style_class->script];

    /* Convert a HarfBuzz script tag into the corresponding OpenType */
    /* tag or tags -- some Indic scripts like Devanagari have an old */
    /* and a new set of features.                                    */
    hb_ot_tags_from_script( script,
                            &script_tags[0],
                            &script_tags[1] );

    /* `hb_ot_tags_from_script' usually returns HB_OT_TAG_DEFAULT_SCRIPT */
    /* as the second tag.  We change that to HB_TAG_NONE except for the  */
    /* default script.                                                   */
    if ( style_class->script == globals->module->default_script &&
         style_class->coverage == AF_COVERAGE_DEFAULT           )
    {
      if ( script_tags[0] == HB_TAG_NONE )
        script_tags[0] = HB_OT_TAG_DEFAULT_SCRIPT;
      else
      {
        if ( script_tags[1] == HB_TAG_NONE )
          script_tags[1] = HB_OT_TAG_DEFAULT_SCRIPT;
        else if ( script_tags[1] != HB_OT_TAG_DEFAULT_SCRIPT )
          script_tags[2] = HB_OT_TAG_DEFAULT_SCRIPT;
      }
    }
    else
    {
      if ( script_tags[1] == HB_OT_TAG_DEFAULT_SCRIPT )
        script_tags[1] = HB_TAG_NONE;
    }

    hb_ot_layout_collect_lookups( face,
                                  HB_OT_TAG_GSUB,
                                  script_tags,
                                  NULL,
                                  coverage_tags,
                                  lookups );

    FT_TRACE4(( "lookups (style `%s'):\n"
                " ",
                af_style_names[style_class->style] ));

#ifdef FT_DEBUG_LEVEL_TRACE
    count = 0;
#endif

    for ( idx = -1; hb_set_next( lookups, &idx ); )
    {
#ifdef FT_DEBUG_LEVEL_TRACE
      FT_TRACE4(( " %d", idx ));
      count++;
#endif

      hb_ot_layout_lookup_collect_glyphs( face,
                                          HB_OT_TAG_GSUB,
                                          idx,
                                          NULL,
                                          NULL,
                                          NULL,
                                          glyphs );
    }

#ifdef FT_DEBUG_LEVEL_TRACE
    if ( !count )
      FT_TRACE4(( " (none)" ));
    FT_TRACE4(( "\n\n" ));

    FT_TRACE4(( "  glyphs (`*' means already assigned)" ));

    count = 0;
#endif

    for ( idx = -1; hb_set_next( glyphs, &idx ); )
    {
#ifdef FT_DEBUG_LEVEL_TRACE
      if ( !( count % 10 ) )
        FT_TRACE4(( "\n"
                    "   " ));

      FT_TRACE4(( " %d", idx ));
      count++;
#endif

      if ( gstyles[idx] == AF_STYLE_UNASSIGNED )
        gstyles[idx] = (FT_Byte)style_class->style;
#ifdef FT_DEBUG_LEVEL_TRACE
      else
        FT_TRACE4(( "*" ));
#endif
    }

#ifdef FT_DEBUG_LEVEL_TRACE
    if ( !count )
      FT_TRACE4(( "\n"
                  "    (none)" ));
    FT_TRACE4(( "\n\n" ));
#endif

    hb_set_destroy( lookups );
    hb_set_destroy( glyphs  );

    hb_face_destroy( face );

    return FT_Err_Ok;
  }


#if 0
  /* to be always excluded */
  COVERAGE(nalt, 'n', 'a', 'l', 't'); /* Alternate Annotation Forms (?) */
  COVERAGE(ornm, 'o', 'r', 'n', 'm'); /* Ornaments (?) */
  /* COVERAGE(ruby, 'r', 'u', 'b', 'y') */ /* (only for Japanese) */
#endif

#endif /* FT_CONFIG_OPTION_USE_HARFBUZZ */


/* END */