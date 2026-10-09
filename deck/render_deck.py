#!/usr/bin/env python3
"""Render proofline-stage1.md to PDF. Supports a small Markdown subset:
# (title page), ## (new page, section heading), - bullets, N. numbered lists,
``` code fences, **bold**, `code` inline. One section per page."""
import re, sys
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor
from reportlab.lib.units import inch
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph,
                                Spacer, PageBreak, ListFlowable, ListItem,
                                Preformatted, Table, TableStyle, HRFlowable,
                                KeepTogether)
from reportlab.lib import colors

NAVY = HexColor("#1b2a4a")
ACCENT = HexColor("#2563eb")
GRAY = HexColor("#4b5563")
LIGHT = HexColor("#f3f4f6")
CODE_BG = HexColor("#f8fafc")

PAGE_W, PAGE_H = letter
MARGIN = 0.95 * inch

st_title = ParagraphStyle("title", fontName="Helvetica-Bold", fontSize=34,
                          leading=40, textColor=NAVY, alignment=TA_CENTER,
                          spaceAfter=10)
st_subtitle = ParagraphStyle("subtitle", fontName="Helvetica", fontSize=15,
                             leading=20, textColor=GRAY, alignment=TA_CENTER,
                             spaceAfter=6)
st_kicker = ParagraphStyle("kicker", fontName="Helvetica-Bold", fontSize=12,
                           leading=16, textColor=ACCENT, alignment=TA_CENTER,
                           spaceAfter=18)
st_h2 = ParagraphStyle("h2", fontName="Helvetica-Bold", fontSize=17,
                       leading=22, textColor=NAVY, spaceAfter=4, keepWithNext=True)
st_body = ParagraphStyle("body", fontName="Helvetica", fontSize=10.5,
                         leading=15.5, textColor=HexColor("#111827"),
                         spaceAfter=7, alignment=TA_LEFT)
st_bullet = ParagraphStyle("bullet", parent=st_body, leftIndent=20,
                           firstLineIndent=0, bulletIndent=8, spaceAfter=4)
st_code = ParagraphStyle("code", fontName="Courier", fontSize=8.2,
                         leading=11.5, textColor=HexColor("#0f172a"))
st_info_key = ParagraphStyle("ik", fontName="Helvetica-Bold", fontSize=10.5,
                             leading=15, textColor=GRAY, alignment=TA_LEFT)
st_info_val = ParagraphStyle("iv", fontName="Helvetica", fontSize=10.5,
                             leading=15, textColor=HexColor("#111827"),
                             alignment=TA_LEFT)

def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def inline(t):
    t = esc(t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"`([^`]+?)`", r'<font face="Courier" size="10">\1</font>', t)
    return t

def parse(md_path):
    with open(md_path) as f:
        lines = f.read().splitlines()
    pages = []          # list of ("title", blocks) or ("section", heading, blocks)
    title_blocks, cur = [], None
    in_title = True
    in_code, code_buf = False, []
    para_buf = []

    def flush_para(blocks):
        if para_buf:
            blocks.append(("p", " ".join(para_buf)))
            para_buf.clear()

    def blocks_for():
        return title_blocks if in_title else cur[2]

    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.strip().startswith("```"):
            if in_code:
                blocks_for().append(("code", "\n".join(code_buf)))
                code_buf = []
                in_code = False
            else:
                flush_para(blocks_for())
                in_code = True
            i += 1
            continue
        if in_code:
            code_buf.append(ln)
            i += 1
            continue
        s = ln.strip()
        if s.startswith("# "):
            title_blocks.append(("maintitle", s[2:].strip()))
        elif s.startswith("### "):
            flush_para(blocks_for())
            title_blocks.append(("kicker", s[4:].strip()))
        elif s.startswith("## "):
            flush_para(blocks_for())
            head = s[3:].strip()
            if re.match(r"^(\d+\.|Appendix)", head):
                in_title = False
                cur = ("section", head, [])
                pages.append(cur)
            else:
                title_blocks.append(("subtitle", head))
        elif re.match(r"^-\s", s):
            flush_para(blocks_for())
            content = s[2:].strip()
            if in_title and ":" in content:
                k, v = content.split(":", 1)
                title_blocks.append(("info", k.strip(), v.strip()))
            else:
                blocks_for().append(("bullet", content))
        elif re.match(r"^\d+\.\s", s):
            flush_para(blocks_for())
            blocks_for().append(("num", re.sub(r"^\d+\.\s", "", s)))
        elif s == "":
            flush_para(blocks_for())
        else:
            para_buf.append(s)
        i += 1
    flush_para(blocks_for())
    return title_blocks, pages

def build_code_block(text):
    # Preformatted takes literal text: do NOT XML-escape (entities show raw).
    pre = Preformatted(text, st_code)
    t = Table([[pre]], colWidths=[PAGE_W - 2 * MARGIN - 16])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), CODE_BG),
        ("BOX", (0, 0), (-1, -1), 0.5, HexColor("#cbd5e1")),
        ("TOPPADDING", (0, 0), (-1, -1), 8),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
        ("LEFTPADDING", (0, 0), (-1, -1), 10),
        ("RIGHTPADDING", (0, 0), (-1, -1), 10),
    ]))
    return t

def build_list(items, numbered):
    # reportlab 5.0.1 mis-renders valueless ListItems; set explicit values.
    lis = [ListItem(Paragraph(inline(x), st_bullet),
                    value=(str(i + 1) if numbered else "\u2022"),
                    leftIndent=20, bulletColor=ACCENT)
           for i, x in enumerate(items)]
    return ListFlowable(lis, bulletType="1" if numbered else "bullet",
                        leftIndent=8)

def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(GRAY)
    canvas.drawString(MARGIN, 0.6 * inch, "ProofLine — Stage I Proposal")
    canvas.drawRightString(PAGE_W - MARGIN, 0.6 * inch, f"{doc.page}")
    canvas.restoreState()

def main(md_path, pdf_path):
    title_blocks, pages = parse(md_path)
    story = []
    # Title page
    story.append(Spacer(1, 1.55 * inch))
    for b in title_blocks:
        if b[0] == "maintitle":
            story.append(Paragraph(esc(b[1]), st_title))
        elif b[0] == "subtitle":
            story.append(Paragraph(esc(b[1]), st_subtitle))
        elif b[0] == "kicker":
            story.append(Paragraph(esc(b[1]), st_kicker))
        elif b[0] == "info":
            pass
    infos = [b for b in title_blocks if b[0] == "info"]
    if infos:
        story.append(Spacer(1, 0.25 * inch))
        rows = [[Paragraph(esc(k), st_info_key), Paragraph(inline(v), st_info_val)]
                for _, k, v in infos]
        t = Table(rows, colWidths=[1.7 * inch, PAGE_W - 2 * MARGIN - 1.7 * inch])
        t.setStyle(TableStyle([
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("TOPPADDING", (0, 0), (-1, -1), 3),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
            ("LINEBELOW", (0, 0), (-1, -2), 0.4, HexColor("#e5e7eb")),
        ]))
        story.append(t)
    story.append(Spacer(1, 0.4 * inch))
    story.append(Paragraph(
        "Deadline: October 14, 2026, 05:00 EDT", st_subtitle))

    for kind, head, blocks in pages:
        story.append(PageBreak())
        story.append(Paragraph(esc(head), st_h2))
        story.append(HRFlowable(width="100%", thickness=1.2, color=ACCENT,
                                spaceAfter=10, spaceBefore=2))
        items, numbered = [], None
        def flush_list():
            nonlocal items, numbered
            if items:
                story.append(build_list(items, numbered))
                story.append(Spacer(1, 4))
                items, numbered = [], None
        for b in blocks:
            if b[0] == "p":
                flush_list()
                story.append(Paragraph(inline(b[1]), st_body))
            elif b[0] == "bullet":
                if numbered:
                    flush_list()
                numbered = False
                items.append(b[1])
            elif b[0] == "num":
                if numbered is False:
                    flush_list()
                numbered = True
                items.append(b[1])
            elif b[0] == "code":
                flush_list()
                story.append(build_code_block(b[1]))
                story.append(Spacer(1, 8))
        flush_list()

    doc = BaseDocTemplate(pdf_path, pagesize=letter,
                          leftMargin=MARGIN, rightMargin=MARGIN,
                          topMargin=0.8 * inch, bottomMargin=0.85 * inch,
                          title="ProofLine — Stage I Proposal",
                          author="Slid Phi Labs")
    frame = Frame(MARGIN, 0.85 * inch, PAGE_W - 2 * MARGIN,
                  PAGE_H - 0.8 * inch - 0.85 * inch, id="main")
    doc.addPageTemplates([PageTemplate(id="p", frames=[frame], onPage=footer)])
    doc.build(story)
    print("wrote", pdf_path)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
