import java.io.PrintWriter
@main def exec(cpgPath: String, outputPath: String) = {
  // joern-parse already ran the call-graph overlays; enhance=false skips the
  // data-flow overlay, which nothing here uses (binutils ran out of heap in it)
  importCpg(cpgPath, enhance = false)
  val pw = new PrintWriter(outputPath)
  def esc(s: String) = s.replace("\\","\\\\").replace("\"","\\\"").replace("\n","\\n").replace("\r","\\r").replace("\t","\\t")
  cpg.method.isExternal(false).foreach { m =>
    val f=m.filename
    if (f.nonEmpty && f!="<empty>" && m.lineNumber.isDefined)
      pw.println(s"""{"kind":"func","name":"${esc(m.name)}","file":"${esc(f)}","line":${m.lineNumber.get},"line_end":${m.lineNumberEnd.getOrElse(m.lineNumber.get)}}""")
  }
  cpg.call.nameNot("<operator>.*").foreach { c =>
    val cm=c.method; val cf=cm.filename
    if (cf.nonEmpty && cf!="<empty>" && cm.lineNumber.isDefined) {
      // 名字边(callsite)
      pw.println(s"""{"kind":"edge","caller":"${esc(cm.name)}","cfile":"${esc(cf)}","cline":${cm.lineNumber.get},"callee":"${esc(c.name)}"}""")
      // 解析边(c.callee 到真实方法)
      c.callee.isExternal(false).foreach { ce =>
        val ef=ce.filename
        if (ef.nonEmpty && ef!="<empty>" && ce.lineNumber.isDefined)
          pw.println(s"""{"kind":"redge","caller":"${esc(cm.name)}","cfile":"${esc(cf)}","cline":${cm.lineNumber.get},"callee":"${esc(ce.name)}","efile":"${esc(ef)}","eline":${ce.lineNumber.get}}""")
      }
    }
  }
  pw.close(); println("WROTE")
}
