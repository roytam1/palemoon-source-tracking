/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Most of this code is copied from mimethsa. If you find a bug here, check that class, too. */

/* This runs the entire HTML document through the Mozilla HTML parser, and
   then outputs it as string again. This ensures that the HTML document is
   syntactically correct and complete and all tags and attributes are closed.

   That prevents "MIME in the middle" attacks like efail.de.
   The base problem is that we concatenate different MIME parts in the output
   and render them all together as a single HTML document in the display.

   The better solution would be to put each MIME part into its own <iframe type="content">.
   during rendering. Unfortunately, we'd need <iframe seamless> for that.
   That would remove the need for this workaround, and stop even more attack classes.
*/

#include "mimeTextHTMLParsed.h"
#include "prmem.h"
#include "prlog.h"
#include "msgCore.h"
#include "nsIDOMParser.h"
#include "nsIDOMDocument.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "mozilla/ErrorResult.h"
#include "nsIPrefBranch.h"
#include "mimethtm.h"

#define MIME_SUPERCLASS mimeInlineTextHTMLClass
MimeDefClass(MimeInlineTextHTMLParsed, MimeInlineTextHTMLParsedClass,
             mimeInlineTextHTMLParsedClass, &MIME_SUPERCLASS);

static int MimeInlineTextHTMLParsed_parse_line(const char *, int32_t,
                                               MimeObject *);
static int MimeInlineTextHTMLParsed_parse_begin(MimeObject *obj);
static int MimeInlineTextHTMLParsed_parse_eof(MimeObject *, bool);
static void MimeInlineTextHTMLParsed_finalize(MimeObject *obj);

static int
MimeInlineTextHTMLParsedClassInitialize(MimeInlineTextHTMLParsedClass *clazz)
{
  MimeObjectClass *oclass = (MimeObjectClass *)clazz;
  NS_ASSERTION(!oclass->class_initialized, "problem with superclass");
  oclass->parse_line  = MimeInlineTextHTMLParsed_parse_line;
  oclass->parse_begin = MimeInlineTextHTMLParsed_parse_begin;
  oclass->parse_eof   = MimeInlineTextHTMLParsed_parse_eof;
  oclass->finalize    = MimeInlineTextHTMLParsed_finalize;

  return 0;
}

static int
MimeInlineTextHTMLParsed_parse_begin(MimeObject *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;
  me->complete_buffer = new nsString();
  int status = ((MimeObjectClass*)&MIME_SUPERCLASS)->parse_begin(obj);
  if (status < 0)
    return status;

  return 0;
}

static int
MimeInlineTextHTMLParsed_parse_eof(MimeObject *obj, bool abort_p)
{

  if (obj->closed_p)
    return 0;
  int status = ((MimeObjectClass*)&MIME_SUPERCLASS)->parse_eof(obj, abort_p);
  if (status < 0)
    return status;
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  // We have to cache all lines and parse the whole document at once.
  // There's a useful sounding function parseFromStream(), but it only allows XML
  // mimetypes, not HTML. Methinks that's because the HTML soup parser
  // needs the entire doc to make sense of the gibberish that people write.
  if (!me || !me->complete_buffer)
    return 0;

  nsString& rawHTML = *(me->complete_buffer);
  if (rawHTML.IsEmpty())
    return 0;
  nsString parsed;
  nsresult rv;

  // Parse the HTML source.
  nsCOMPtr<nsIDOMDocument> document;
  nsCOMPtr<nsIDOMParser> parser = do_GetService(NS_DOMPARSER_CONTRACTID);
  rv = parser->ParseFromString(rawHTML.get(), "text/html",
                               getter_AddRefs(document));
  NS_ENSURE_SUCCESS(rv, -1);

  // Serialize it back to HTML source again.
  nsCOMPtr<nsIDocumentEncoder> encoder = do_CreateInstance(
    "@mozilla.org/layout/documentEncoder;1?type=text/html");
  uint32_t aFlags = nsIDocumentEncoder::OutputRaw |
                    nsIDocumentEncoder::OutputDisallowLineBreaking;
  rv = encoder->Init(document, NS_LITERAL_STRING("text/html"), aFlags);
  NS_ENSURE_SUCCESS(rv, -1);
  rv = encoder->EncodeToString(parsed);
  NS_ENSURE_SUCCESS(rv, -1);

  // Write it out.
  NS_ConvertUTF16toUTF8 resultCStr(parsed);
  MimeInlineTextHTML_insert_lang_div(obj, resultCStr);
  MimeInlineTextHTML_remove_plaintext_tag(obj, resultCStr);
  status = ((MimeObjectClass*)&MIME_SUPERCLASS)->parse_line(
    resultCStr.BeginWriting(), resultCStr.Length(), obj);
  rawHTML.Truncate();
  return status;
}

void
MimeInlineTextHTMLParsed_finalize(MimeObject *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  if (me && me->complete_buffer)
  {
    obj->clazz->parse_eof(obj, false);
    delete me->complete_buffer;
    me->complete_buffer = NULL;
  }

  ((MimeObjectClass*)&MIME_SUPERCLASS)->finalize(obj);
}

static int
MimeInlineTextHTMLParsed_parse_line(const char *line, int32_t length,
                                    MimeObject *obj)
{
  MimeInlineTextHTMLParsed *me = (MimeInlineTextHTMLParsed *)obj;

  if (!me || !(me->complete_buffer))
    return -1;

  nsCString linestr(line, length);
  NS_ConvertUTF8toUTF16 line_ucs2(linestr.get());
  if (length && line_ucs2.IsEmpty())
    CopyASCIItoUTF16(linestr, line_ucs2);
  (me->complete_buffer)->Append(line_ucs2);

  return 0;
}
