#![feature(string_remove_matches)]
use anyhow::{anyhow, Context};
use std::{
    borrow::Cow,
    cell::{Ref, RefCell, RefMut},
    collections::{BTreeMap, HashSet},
    iter::repeat,
    ops::{Deref, DerefMut},
    path::{Path, PathBuf},
    rc::Rc,
};

use clang::{
    documentation::{Comment, CommentChild},
    EntityKind,
};
use clap::Parser;
use std::io::Write;

#[derive(Parser, Debug)]
struct Options {
    input:  PathBuf,
    output: PathBuf,
}

#[derive(Hash, PartialEq, Eq, Clone, Debug, PartialOrd, Ord)]
enum Type {
    Base { namespace: Option<String>, member: String },
    Array(Box<Type>),
}

struct SimpleTypeDisplay<'a>(&'a Type);
impl std::fmt::Display for SimpleTypeDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            Type::Base { member, .. } => write!(f, "{}", member),
            Type::Array(inner) => {
                write!(f, "[{}]", inner.simple_display())
            }
        }
    }
}

struct RstTypeDisplay<'a>(&'a Type);
impl std::fmt::Display for RstTypeDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            Type::Base { namespace, member } => {
                if let Some(ns) = namespace {
                    write!(f, "{ns}.")?;
                }
                write!(f, "{member}")
            }
            Type::Array(inner) => write!(f, "{}", inner.rst_display()),
        }
    }
}

impl Type {
    fn base_type(&self) -> &Type {
        match self {
            Self::Base { .. } => self,
            Self::Array(inner) => inner.base_type(),
        }
    }
    fn namespace(&self) -> Option<&Option<String>> {
        match self {
            Self::Base { namespace, .. } => Some(namespace),
            _ => None,
        }
    }
    fn member(&self) -> Option<&String> {
        match self {
            Self::Base { member, .. } => Some(member),
            _ => None,
        }
    }
    fn simple_display(&self) -> SimpleTypeDisplay {
        SimpleTypeDisplay(self)
    }
    fn rst_display(&self) -> RstTypeDisplay {
        RstTypeDisplay(self)
    }
}

impl std::fmt::Display for Type {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Base { namespace, member } => {
                if let Some(ns) = namespace {
                    write!(f, "{}", ns)?;
                }
                write!(f, ":{}", member)
            }
            Self::Array(inner) => write!(f, "[{}]", inner),
        }
    }
}

#[derive(Debug)]
struct Parameter {
    name: String,
    ty:   Option<Type>,
    doc:  String,
}

impl std::fmt::Display for Parameter {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.name)?;
        if let Some(ty) = &self.ty {
            write!(f, ": {}", ty.simple_display())?;
        }
        Ok(())
    }
}
#[derive(Debug, Default)]
struct Doc {
    /// A brief description
    brief: String,
    /// An array of paragraphs
    body:  Vec<String>,
}
/// How can a type be accessed?
#[derive(Debug, Hash, PartialEq, Eq, Clone, PartialOrd, Ord)]
enum Access<'a> {
    /// As descendent of the root object
    Ancestry { path: Cow<'a, str> },
    /// As member of a type
    Member { ty: Type, member: Cow<'a, str> },
}

impl Access<'_> {
    fn is_ancestry(&self) -> bool {
        match self {
            Self::Ancestry { .. } => true,
            _ => false,
        }
    }
}

impl std::fmt::Display for Access<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Ancestry { path } => write!(f, "{}", path),
            Self::Member { ty, member } => write!(f, "{}.{}", ty, member),
        }
    }
}
#[derive(Debug)]
struct Entry {
    /// How can this export be accessed. If None, this export is accessed indirectly via return
    /// types of methods, or as a property.
    path:     Access<'static>,
    /// List of parameters to the method, `None` if this is a property
    params:   Option<Vec<Parameter>>,
    ty:       Option<Type>,
    doc:      Option<Doc>,
    children: BTreeMap<String, Rc<RefCell<Entry>>>,
}

struct EntryDisplay<'a>(&'a Entry);

impl std::fmt::Display for EntryDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0.path.base_name())?;
        if let Some(params) = &self.0.params {
            write!(f, "({})", params.iter().map(|x| &x.name).join(", "))?;
        }
        Ok(())
    }
}

impl Entry {
    fn display(&self) -> EntryDisplay {
        EntryDisplay(self)
    }
    fn role(&self) -> &'static str {
        if self.params.is_none() {
            "lua:attr"
        } else if self.ty.is_none() {
            "lua:sgnl"
        } else {
            "lua:meth"
        }
    }
    fn directive(&self) -> &'static str {
        if self.params.is_none() {
            "lua:attribute"
        } else if self.ty.is_none() {
            "lua:signal"
        } else {
            "lua:method"
        }
    }
}

#[derive(Default, Debug)]
struct TypeEntry {
    references:   HashSet<Access<'static>>,
    children:     BTreeMap<String, Rc<RefCell<Entry>>>,
    has_ancestor: bool,
    doc:          Doc,
}
trait HasChildren {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>>;
}
trait HasChildrenMut: HasChildren {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>>;
}
impl HasChildren for TypeEntry {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        &self.children
    }
}
impl HasChildrenMut for TypeEntry {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        &mut self.children
    }
}
impl HasChildren for Entry {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        &self.children
    }
}
impl HasChildrenMut for Entry {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        &mut self.children
    }
}
use either::Either;
impl<A, B> HasChildren for Either<A, B>
where
    A: Deref,
    B: Deref,
    <A as Deref>::Target: HasChildren,
    <B as Deref>::Target: HasChildren,
{
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        match self {
            Either::Left(a) => a.children(),
            Either::Right(a) => a.children(),
        }
    }
}
impl<A, B> HasChildrenMut for Either<A, B>
where
    A: DerefMut,
    B: DerefMut,
    <A as Deref>::Target: HasChildrenMut,
    <B as Deref>::Target: HasChildrenMut,
{
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        match self {
            Either::Left(a) => a.children_mut(),
            Either::Right(a) => a.children_mut(),
        }
    }
}
use itertools::Itertools;
#[derive(Debug)]
enum AccessOrType<'a> {
    Access(Access<'a>),
    Type(&'a Type),
}
impl std::fmt::Display for AccessOrType<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Access(a) => write!(f, "{}", a),
            Self::Type(t) => write!(f, "{}", t),
        }
    }
}
impl Access<'_> {
    fn parent(&self) -> Option<AccessOrType> {
        use utils::namespace::Namespace;
        match self {
            Self::Ancestry { path } => path
                .parent()
                .map(|path| AccessOrType::Access(Access::Ancestry { path: path.into() })),
            Self::Member { ty, .. } => Some(AccessOrType::Type(&ty)),
        }
    }
    fn base_name(&self) -> &str {
        use utils::namespace::Namespace;
        match self {
            Self::Ancestry { path } => path.last(),
            Self::Member { member, .. } => member,
        }
    }
}

#[derive(Debug)]
struct Docs {
    by_access: BTreeMap<Access<'static>, Rc<RefCell<Entry>>>,
    by_type:   BTreeMap<Type, RefCell<TypeEntry>>,
    has_error: bool,
}
mod parsers {
    use std::collections::HashMap;

    use itertools::Itertools;
    use nom::branch::alt;
    use nom::bytes::complete::is_a;
    use nom::bytes::complete::tag;
    use nom::character::complete::multispace0;
    use nom::character::complete::space0;
    use nom::combinator::map;
    use nom::combinator::opt;
    use nom::error::ParseError;
    use nom::multi::many0;
    use nom::multi::separated_list0;
    use nom::multi::separated_list1;
    use nom::sequence::delimited;
    use nom::sequence::preceded;
    use nom::sequence::separated_pair;
    use nom::sequence::terminated;
    use nom::sequence::tuple;
    use nom::AsChar;
    use nom::Compare;
    use nom::IResult;
    use nom::InputTake;
    use nom::InputTakeAtPosition;

    use crate::Access;

    use super::{Entry, Parameter, Type};

    /// Matches tag ignoring white spaces
    fn tag2<Input, Error: ParseError<Input>>(
        s: &'static str,
    ) -> impl FnMut(Input) -> IResult<Input, Input, Error>
    where
        Input: InputTakeAtPosition + Compare<&'static str> + InputTake,
        <Input as InputTakeAtPosition>::Item: AsChar + Clone,
    {
        delimited(multispace0, tag(s), multispace0)
    }

    fn ident(s: &str) -> IResult<&str, String> {
        is_a("1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")(s).and_then(
            |(i, o)| {
                if o.len() > 0 {
                    Ok((i, o.to_owned()))
                } else {
                    Err(nom::Err::Failure(nom::error::Error::new(i, nom::error::ErrorKind::IsA)))
                }
            },
        )
    }
    fn namespaced_ident(s: &str) -> IResult<&str, String> {
        separated_list1(tag("."), ident)(s).map(|(i, o)| (i, o.into_iter().join(".")))
    }
    fn parameters(s: &str) -> IResult<&str, Vec<Parameter>> {
        separated_list0(tag2(","), ident)(s).map(|(i, o)| {
            (
                i,
                o.into_iter()
                    .map(|s| Parameter { name: s, ty: None, doc: String::new() })
                    .collect(),
            )
        })
    }
    fn access(s: &str) -> IResult<&str, Access<'static>> {
        alt((
            map(separated_pair(base_type, tag("."), ident), |(ty, member)| Access::Member {
                ty,
                member: member.into(),
            }),
            map(namespaced_ident, |path| Access::Ancestry { path: path.into() }),
        ))(s)
    }
    fn declaration(s: &str) -> IResult<&str, (Access<'static>, Option<Vec<Parameter>>)> {
        let param_list = delimited(tag2("("), parameters, tag2(")"));
        tuple((access, opt(param_list)))(s)
    }
    fn base_type(s: &str) -> IResult<&str, Type> {
        tuple((opt(namespaced_ident), preceded(tag(":"), ident)))(s)
            .map(|(i, (o1, o2))| (i, Type::Base { namespace: o1, member: o2 }))
    }
    // No nested array types
    fn array_type(s: &str) -> IResult<&str, Type> {
        delimited(tag2("["), base_type, tag2("]"))(s).map(|(i, o)| (i, Type::Array(Box::new(o))))
    }

    fn param_detail_line(s: &str) -> IResult<&str, Parameter> {
        let mut prefix = map(
            preceded(
                tag2("-"),
                tuple((
                    ident,
                    opt(delimited(
                        tag2("("),
                        alt((array_type, base_type)),
                        preceded(multispace0, tag(")")),
                    )),
                )),
            ),
            |(name, ty)| Parameter { name, ty, doc: String::new() },
        );
        let (rest, mut param) = prefix(s)?;

        let (rest, _) = space0(rest)?;
        let mut rest = rest.chars();
        while let Some(ch) = rest.next() {
            if ch == '\n' || ch == '\r' {
                // Skip white space after newline
                let (next_ch, _) = multispace0(rest.as_str())?;
                if prefix(next_ch).is_ok() {
                    // We reached the beginning of the next param line
                    break
                }
                param.doc.push(' ');
                rest = next_ch.chars();
            } else {
                param.doc.push(ch);
            }
        }
        Ok((rest.as_str(), param))
    }

    pub(crate) fn param_detail_lines(s: &str) -> IResult<&str, HashMap<String, Parameter>> {
        map(many0(param_detail_line), |lines| {
            lines.into_iter().map(|p| (p.name.clone(), p)).collect()
        })(s)
    }

    pub(crate) fn signal_line(s: &str) -> IResult<&str, Entry> {
        map(preceded(tuple((tag("SIGNAL:"), space0)), declaration), |(access, params)| Entry {
            path: access,
            params,
            ty: None,
            doc: None,
            children: Default::default(),
        })(s)
    }

    pub(crate) fn export_line(s: &str) -> IResult<&str, Entry> {
        preceded(
            tuple((tag("EXPORT:"), space0)),
            tuple((terminated(declaration, tag2(",")), alt((array_type, base_type)))),
        )(s)
        .map(|(i, ((path, params), o2))| {
            (i, Entry { path, params, ty: Some(o2), doc: None, children: Default::default() })
        })
    }

    pub(crate) fn type_line(s: &str) -> IResult<&str, Type> {
        preceded(tuple((tag("TYPE:"), space0)), base_type)(s)
    }
}

mod utils {
    pub(super) mod namespace {
        pub trait Namespace {
            fn is_ancestor(&self, other: &Self) -> bool;
            fn validate(&self) -> bool;
            fn parent(&self) -> Option<&Self>;
            fn last(&self) -> &Self;
        }
        impl Namespace for str {
            /// Check if namespace `a` is a ancestor of namespace `b`
            fn is_ancestor(&self, b: &str) -> bool {
                b.strip_prefix(self).and_then(|rest| rest.chars().next()) == Some('.')
            }

            fn validate(&self) -> bool {
                self.split('.').all(|x| x.len() != 0)
            }

            fn parent(&self) -> Option<&str> {
                self.rsplit_once('.').map(|(a, _)| a)
            }
            fn last(&self) -> &str {
                self.rsplit_once('.').map(|(_, b)| b).unwrap_or(self)
            }
        }
    }
}

impl Docs {
    fn new() -> Self {
        Self { by_access: Default::default(), by_type: Default::default(), has_error: false }
    }
    fn collect_paragraphs(comment: &Comment) -> Vec<String> {
        comment
            .get_children()
            .into_iter()
            .filter_map(|child| match child {
                CommentChild::Paragraph(children) => Some(
                    children
                        .into_iter()
                        .map(|child| match child {
                            CommentChild::Text(txt) => {
                                txt.strip_prefix(" ").map(|x| x.to_owned()).unwrap_or(txt)
                            }
                            _ => String::new(),
                        })
                        .join("\n")
                        .to_owned(),
                ),
                _ => None,
            })
            .collect()
    }
    fn handle_comment_inner(&mut self, comment: &Comment) -> anyhow::Result<()> {
        let paragraphs = Self::collect_paragraphs(comment);
        let mut brief = None;
        let mut body = vec![];
        let mut entry: Option<Entry> = None;
        let mut export_seen = false;

        let mut save_entry =
            |entry: &mut Option<Entry>, brief: &mut Option<String>, body: &mut Vec<String>| {
                if let Some(mut entry) = entry.take() {
                    log::info!("Added {}", entry.display());
                    let new_body = vec![];
                    let body = std::mem::replace(body, new_body);
                    entry.doc = brief.take().map(|brief| Doc { brief, body });
                    let path = entry.path.clone();
                    let entry = Rc::new(RefCell::new(entry));
                    self.by_access.insert(path, entry.clone());
                }
            };

        let mut paragraphs = paragraphs.into_iter();
        while let Some(p) = paragraphs.next() {
            if p.starts_with("EXPORT:") {
                log::debug!("Export line {}", p);
                let signature = parsers::export_line(&p);
                let signature =
                    signature.map(|(_, a)| a).map_err(|e| anyhow!("Invalid export line: {}", e))?;
                if !export_seen {
                    save_entry(&mut entry, &mut brief, &mut body);
                    entry = Some(signature);
                    export_seen = true;
                } else {
                    return Err(anyhow!("Multiple export lines: {}", p))
                }
            } else if p.starts_with("SIGNAL:") {
                log::debug!("Signal line: {}", p);
                let (next, signature) =
                    parsers::signal_line(&p).map_err(|e| anyhow!("Invalid signal line: {}", e))?;
                save_entry(&mut entry, &mut brief, &mut body);
                brief = Some(next.trim().to_owned());
                entry = Some(signature);
            } else if p == "Arguments:" {
                let p = paragraphs.next().context("Arguments: not followed by parameter block")?;
                let (_, mut param_detail) = parsers::param_detail_lines(&p).map_err(|_| {
                    anyhow!("Parameter block not proceeded by a signal or export line")
                })?;
                if let Some(entry) = &mut entry {
                    if let Some(params) = &mut entry.params {
                        for p in params {
                            if let Some(detail) = param_detail.get_mut(&p.name) {
                                if !p.doc.is_empty() || p.ty.is_some() {
                                    return Err(anyhow!(
                                        "Duplicated documentation for parameter {}",
                                        p.name
                                    ))
                                }
                                p.doc = std::mem::replace(&mut detail.doc, String::new());
                                p.ty = detail.ty.take();
                            }
                        }
                    } else {
                        return Err(anyhow!("Parameter block attached to a property"))
                    }
                }
            } else {
                if brief.is_none() {
                    brief = Some(p);
                } else {
                    body.push(p);
                }
            }
        }
        save_entry(&mut entry, &mut brief, &mut body);
        Ok(())
    }
    fn handle_comment(&mut self, comment: &Comment) {
        let ret = self.handle_comment_inner(comment);
        log::debug!("{:?}", ret);
        if let Err(e) = ret {
            self.has_error = true;
            log::error!("{}", e);
        }
    }
    fn handle_type_comment(&mut self, comment: &Comment) -> Option<&mut RefCell<TypeEntry>> {
        let paragraphs = Self::collect_paragraphs(comment);
        let mut brief = None;
        let mut body = vec![];
        let mut entry = None;
        for p in paragraphs {
            if p.starts_with("TYPE:") {
                if let Ok(ty) = parsers::type_line(&p) {
                    if entry.is_none() {
                        entry = Some(ty.1);
                    } else {
                        self.has_error = true;
                        log::error!("Multiple export lines: {}", p);
                    }
                } else {
                    self.has_error = true;
                    log::error!("Invalid export line: {}", p);
                    return None
                }
            } else {
                if brief.is_none() {
                    brief = Some(p);
                } else {
                    body.push(p);
                }
            }
        }
        entry.map(|entry| {
            let entry = self.by_type.entry(entry).or_insert_with(Default::default);
            entry.borrow_mut().doc.brief = brief.unwrap_or(String::new());
            entry.borrow_mut().doc.body.extend(body.into_iter());
            entry
        })
    }
    fn with_entry<T>(
        &self,
        key: &AccessOrType,
        f: impl FnOnce(Either<Ref<Entry>, Ref<TypeEntry>>) -> T,
    ) -> Option<T> {
        match &key {
            AccessOrType::Access(access) => {
                self.by_access.get(&access).map(|a| f(Either::Left(a.borrow())))
            }
            AccessOrType::Type(ty) => self.by_type.get(&ty).map(|a| f(Either::Right(a.borrow()))),
        }
    }
    fn with_entry_mut<T>(
        &self,
        key: &AccessOrType,
        f: impl FnOnce(Either<RefMut<Entry>, RefMut<TypeEntry>>) -> T,
    ) -> Option<T> {
        match &key {
            AccessOrType::Access(access) => {
                self.by_access.get(&access).map(|a| f(Either::Left(a.borrow_mut())))
            }
            AccessOrType::Type(ty) => {
                self.by_type.get(&ty).map(|a| f(Either::Right(a.borrow_mut())))
            }
        }
    }
    fn build_tree(&mut self) {
        for (k, v) in self.by_access.iter() {
            if let Some(ty) = &v.borrow().ty {
                let te = self.by_type.entry(ty.base_type().clone()).or_insert_with(|| {
                    log::info!("Added type {}", ty.base_type());
                    Default::default()
                });
                te.borrow_mut().references.insert(k.clone());
                if k.is_ancestry() && v.borrow().params.is_none() {
                    te.borrow_mut().has_ancestor = true;
                }
            } else {
                if let Some(params) = &v.borrow().params {
                    // Add references for types used in a signal
                    for p in params {
                        if let Some(ty) = &p.ty {
                            let te = self
                                .by_type
                                .entry(ty.base_type().clone())
                                .or_insert_with(Default::default);
                            te.borrow_mut().references.insert(k.clone());
                        }
                    }
                }
            }
        }
        let mut has_error = false;
        for (k, v) in self.by_access.iter() {
            if let Some(parent) = k.parent() {
                let old = self.with_entry_mut(&parent, |mut e| {
                    e.children_mut().insert(k.base_name().to_owned(), v.clone())
                });
                if let Some(old) = old {
                    if let Some(old) = old {
                        log::error!("Duplicated entry {}", old.borrow().path);
                        has_error = true;
                    }
                } else {
                    log::error!("Missing parent {}", parent);
                    has_error = true;
                }
            }
        }
        self.has_error |= has_error;
    }
    fn print_entries<'a, It, W: std::io::Write>(it: It, mut output: W) -> std::io::Result<()>
    where
        It: Iterator<Item = (&'a String, &'a Rc<RefCell<Entry>>)> + Clone,
    {
        for (name, child) in it.clone() {
            let child = child.borrow();
            writeln!(
                output,
                "   * - :{}:`{} <{name}>`\n     - {}",
                child.role(),
                child.display(),
                child.doc.as_ref().map(|x| x.brief.as_str()).unwrap_or("")
            )?;
        }
        writeln!(output)?;
        let ref_type = |output: &mut W, ty: &Type| -> std::io::Result<()> {
            let base_type = ty.base_type();
            if base_type.namespace().unwrap().is_none() {
                // A fundamental type
                writeln!(output, "{}", ty.simple_display())?;
            } else {
                writeln!(output, ":lua:mod:`{} <{}>`", ty.simple_display(), ty.rst_display(),)?;
            }
            Ok(())
        };
        for (_, child) in it.clone() {
            let child = child.borrow();
            write!(output, ".. {}:: {}", child.directive(), child.display())?;
            if let Some(ty) = &child.ty {
                writeln!(
                    output,
                    "{} {}\n",
                    if child.params.is_none() { ":" } else { " ->" },
                    ty.simple_display(),
                )?;
                let type_tag = if child.params.is_none() { "type" } else { "rtype" };
                write!(output, "   :{type_tag}: ")?;
                ref_type(&mut output, child.ty.as_ref().unwrap())?;
            } else {
                writeln!(output, "\n")?;
            }

            // Print parameters

            for p in child.params.iter().flatten() {
                if !p.doc.is_empty() {
                    writeln!(output, "   :param {}: {}", p.name, p.doc)?;
                }
                if let Some(ty) = &p.ty {
                    write!(output, "   :type {}: ", p.name)?;
                    ref_type(&mut output, ty)?;
                    if p.doc.is_empty() {
                        writeln!(output, "   :param {}:", p.name)?;
                    }
                }
            }
            writeln!(output)?;
            if let Some(doc) = &child.doc {
                writeln!(output, "   {} \n", doc.brief)?;
                for p in &doc.body {
                    writeln!(
                        output,
                        "{}\n",
                        p.split("\n").map(|p| format!("   {}", p)).join("\n")
                    )?;
                }
            }
        }
        Ok(())
    }
    fn generate_single(&self, entry: AccessOrType<'_>, target: &Path) -> std::io::Result<()> {
        self.with_entry(&entry, |e| {
            let children = e.children();
            let mut output = std::fs::File::create(target)?;
            let title = match &e {
                Either::Left(entry) => {
                    assert!(entry.path.is_ancestry());
                    assert!(entry.params.is_none());
                    let path = entry.path.to_string();
                    let title = if entry.ty.as_ref().unwrap().to_string() == "deai:module" {
                        format!("Module {}", path)
                    } else {
                        format!("Type {} ({})", entry.ty.as_ref().unwrap().base_type(), path)
                    };
                    title
                }
                Either::Right(_) => {
                    format!("Type {}", entry)
                }
            };
            writeln!(output, "{}", repeat("=").take(title.len()).join(""))?;
            writeln!(output, "{}", title)?;
            writeln!(output, "{}\n", repeat("=").take(title.len()).join(""))?;

            match &e {
                Either::Left(entry) => {
                    writeln!(output, ".. lua:module:: {}\n", entry.path)?;
                    if let Some(doc) = &entry.doc {
                        writeln!(output, "{}\n", doc.brief)?;
                        for p in &doc.body {
                            writeln!(output, "{}\n", p)?;
                        }
                    }
                }
                Either::Right(type_entry) => {
                    match entry {
                        AccessOrType::Type(ty) => {
                            writeln!(output, ".. lua:module:: {}\n", ty.rst_display())?
                        }
                        AccessOrType::Access(_) => unreachable!(),
                    }
                    if !type_entry.doc.brief.is_empty() {
                        writeln!(output, "{}\n", type_entry.doc.brief)?;
                        for p in &type_entry.doc.body {
                            writeln!(output, "{}\n", p)?;
                        }
                    }
                    let references = type_entry
                        .references
                        .iter()
                        .map(|ref_| match ref_ {
                            Access::Ancestry { path } => format!(":lua:meth:`{}`", path),
                            Access::Member { ty, member } => format!(
                                ":lua:attr:`{}.{member} <{}.{member}>`",
                                ty,
                                ty.rst_display(),
                            ),
                        })
                        .join(", ");
                    writeln!(output, "See {references} for more information about this type\n")?;
                }
            }
            let prop_it = children.iter().filter(|(_, c)| {
                let c = c.borrow();
                c.params.is_none() && c.ty.is_some()
            });
            if prop_it.clone().count() > 0 {
                writeln!(
                    output,
                    "Properties\n==========\n\n.. list-table::\n   :header-rows: 0\n"
                )?;
                Self::print_entries(prop_it, &mut output)?;
            }

            let method_it = children.iter().filter(|(_, c)| {
                let c = c.borrow();
                c.params.is_some() && c.ty.is_some()
            });
            if method_it.clone().count() > 0 {
                writeln!(output, "Methods\n==========\n\n.. list-table::\n   :header-rows: 0\n")?;
                Self::print_entries(method_it, &mut output)?;
            }
            let signal_it = children.iter().filter(|(_, c)| c.borrow().ty.is_none());
            if signal_it.clone().count() > 0 {
                writeln!(output, "Signals\n==========\n\n.. list-table::\n   :header-rows: 0\n")?;
                Self::print_entries(signal_it, &mut output)?;
            }
            Ok(())
        })
        .unwrap()
    }
    fn generate(&self, output: &Path) -> std::io::Result<()> {
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() {
                continue
            }
            if entry.borrow().params.is_some() {
                continue
            }
            self.generate_single(
                AccessOrType::Access(access.clone()),
                &output.join(format!("{}.rst", access)),
            )?;
        }

        for (k, type_entry) in &self.by_type {
            if type_entry.borrow().has_ancestor {
                continue
            }
            if k.namespace().unwrap().is_none() {
                continue
            }
            self.generate_single(AccessOrType::Type(k), &output.join(format!("{}.rst", k)))?;
        }

        // Generate toc files
        let mut modules = std::fs::File::create(output.join("modules.rst"))?;
        writeln!(modules, "===========\n**Modules**\n===========\n")?;
        writeln!(modules, ".. toctree::\n   :maxdepth: 2\n")?;
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() {
                continue
            }
            if entry.borrow().params.is_some() {
                continue
            }
            writeln!(modules, "   {access} <{access}>")?;
        }

        let mut types = std::fs::File::create(output.join("types.rst"))?;
        writeln!(types, "=========\n**Types**\n=========\n")?;
        writeln!(types, ".. toctree::\n   :maxdepth: 2\n")?;
        for (k, type_entry) in &self.by_type {
            if type_entry.borrow().has_ancestor {
                continue
            }
            if k.namespace().unwrap().is_none() {
                continue
            }
            writeln!(types, "   {k} <{k}>")?;
        }
        // Generate index
        let mut index = std::fs::File::create(output.join("index.rst"))?;
        writeln!(index, "========\n**deai**\n========\n")?;
        writeln!(index, "=======\nModules\n=======\n")?;

        writeln!(index, ".. toctree::\n   :maxdepth: 3\n   :hidden:\n\n   modules\n   types\n")?;
        writeln!(index, ".. list-table::\n   :header-rows: 0\n")?;
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() || access.parent().is_some() {
                continue
            }
            writeln!(
                index,
                "   * - :doc:`{access} <{access}>`\n     - {}",
                entry.borrow().doc.as_ref().map(|x| x.brief.as_str()).unwrap_or("")
            )?;
        }

        Ok(())
    }
}
fn main() {
    env_logger::init();
    let mut docs = Docs::new();
    let args = Options::parse();
    let clang = clang::Clang::new().unwrap();
    let cdb = clang::CompilationDatabase::from_directory(&args.input).unwrap();
    let index = clang::Index::new(&clang, false, true);
    let clang_include = format!(
        "-I/usr/lib/clang/{}/include",
        clang::get_version().strip_prefix("clang version ").unwrap()
    );
    for cmd in cdb.get_all_compile_commands().get_commands() {
        if !cmd.get_filename().display().to_string().contains("plugins") {
            continue
        }
        let full_name = cmd.get_directory().join(cmd.get_filename());
        log::debug!("{}", full_name.display());
        let mut parser = index.parser(&full_name);
        parser.skip_function_bodies(true);
        let mut args: Vec<_> = cmd
            .get_arguments()
            .into_iter()
            .skip(1)
            .filter_map(|s| {
                if s.starts_with('-') && s != "-MF" && s != "-MD" && s != "-MQ" && s != "-o" {
                    if s.starts_with("-I") {
                        // canonlize the path
                        let full_include_dir =
                            cmd.get_directory().join(std::path::Path::new(&s[2..]));
                        Some(format!("-I{}", full_include_dir.display()))
                    } else {
                        Some(s)
                    }
                } else {
                    None
                }
            })
            .collect();
        args.push(clang_include.clone());
        parser.arguments(&args);
        let tu = parser.parse().unwrap();
        //println!("{:?} {:?}", full_name, tu);
        let entity = tu.get_entity();
        //println!("{:?}", entity.get_comment());
        for child in entity.get_children() {
            // TODO: parse struct comments and members
            // TODO: parse SIGNAL lines
            if !child.get_range().map(|x| x.is_in_main_file()).unwrap_or(false) {
                continue
            }
            //println!("\t{:?}", child.get_kind());
            match child.get_kind() {
                EntityKind::FunctionDecl => {
                    if let Some(comment) = child.get_parsed_comment() {
                        docs.handle_comment(&comment);
                    }
                }
                EntityKind::StructDecl => {
                    if let Some(comment) = child.get_parsed_comment() {
                        if let Some(_) = docs.handle_type_comment(&comment) {
                            for member in child.get_children() {
                                log::debug!("{member:?}");
                                if member.get_kind() == EntityKind::FieldDecl {
                                    if let Some(comment) = member.get_parsed_comment() {
                                        docs.handle_comment(&comment);
                                    }
                                }
                            }
                        }
                    }
                }
                _ => (),
            }
        }
    }
    docs.build_tree();
    if !Path::new(&args.output).exists() {
        std::fs::create_dir(&args.output).unwrap();
    }
    docs.generate(&args.output).unwrap();
}
