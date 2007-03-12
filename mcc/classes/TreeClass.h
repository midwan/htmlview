#ifndef TREECLASS_H
#define TREECLASS_H

#include "SuperClass.h"

// forward declarations

class TreeClass : public SuperClass
{
	public:
		TreeClass () : SuperClass()
		{
			Flags |= FLG_Tree | FLG_Virgin;
			Last = (struct ChildsList *)&FirstChild;
		}
		virtual ~TreeClass ();

		virtual VOID Parse (REG(a2, struct ParseMessage &pmsg));
		virtual VOID AppendGadget (struct AppendGadgetMessage &amsg);
		virtual VOID MinMax (struct MinMaxMessage &mmsg);
		virtual BOOL Layout (struct LayoutMessage &lmsg);
		virtual VOID AdjustPosition (LONG x, LONG y);
		virtual VOID Relayout (BOOL all);
		virtual VOID GetImages (struct GetImagesMessage &gmsg);
		virtual VOID Render (struct RenderMessage &rmsg);
		virtual VOID AddChild (class SuperClass *obj);
		virtual VOID AllocateColours (struct ColorMap *cmap);
		virtual VOID FreeColours (struct ColorMap *cmap);
		virtual VOID ExportForm (struct ExportFormMessage &emsg);
		virtual VOID ResetForm ();
		virtual BOOL HitTest (struct HitTestMessage &hmsg);
		virtual class AClass *FindAnchor (STRPTR name);
		virtual class MapClass *FindMap (STRPTR name);
		virtual BOOL UseMap (struct UseMapMessage &umsg);
		virtual class SuperClass *FindElement (ULONG tagID);
		virtual class TextClass *Find (struct FindMessage &fmsg);
		virtual BOOL Mark (struct MarkMessage &mmsg);
    virtual LONG AllocPen(struct ColorMap *cmap, LONG t_rgb);
    virtual VOID FreePen(struct ColorMap *cmap, ULONG pen) { ReleasePen(cmap, pen); }
    virtual VOID SetCol(LONG &storage, ULONG pen);

		struct ChildsList *Last;
		struct ChildsList *FirstChild;

  protected:
    APTR Backup(struct ParseMessage &pmsg, ULONG len, ULONG tags, ...);
    VOID Restore(UBYTE *opencounts, ULONG len, APTR handle);
};

#endif // TREECLASS_H