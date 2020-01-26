#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QPainter>
#include <QRawFont>
#include <iostream>
#include <fstream>
#include <algorithm>

template<typename T>
void write(std::ofstream &s, const T &v, size_t l = sizeof(T))
{
    s.write((char*)&v, l);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_browse_button_clicked()
{
    ui->path_text->setText(QFileDialog::getOpenFileName(this, tr("Choose TTF/OTF File"),
        "./fonts", tr("Font Files") + QString(" (*.ttf; *.otf)")));
}

void MainWindow::on_convert_button_clicked()
{
    auto raw_font = QRawFont(ui->path_text->text(), 256.0, QFont::HintingPreference::PreferNoHinting);

    ui->info_text->clear();
    ui->info_text->append("-----------------------");
    ui->info_text->append("Ascent: " + QString::number(raw_font.ascent()));
    ui->info_text->append("Descent: " + QString::number(raw_font.descent()));

    auto glyph_paths = std::vector<std::vector<unsigned int>>{};
    for (int c = ui->start_range->value(); c <= ui->end_range->value(); ++c)
    {
        unsigned int gi;
        int ng = 1;
        QChar qc(c);
        raw_font.glyphIndexesForChars(&qc, 1, &gi, &ng);
        QPainterPath path = raw_font.pathForGlyph(gi);
        QRectF bounds = path.boundingRect();

        ui->info_text->append("-----------------------");
        ui->info_text->append("Path for char: " + QString::number(c));
        ui->info_text->append("Width: " + QString::number(bounds.width()));
        ui->info_text->append("-----------------------");

        auto glyph_info = std::vector<unsigned int>{};
        glyph_info.reserve(path.elementCount() * 3 + 2);
        glyph_info.push_back(bounds.width() * (1 << 16));
        auto len = glyph_info.size();
        glyph_info.push_back(path.elementCount() * (3 * sizeof(unsigned int)));

        int x = 0, y = 0;
        for (int i = 0; i < path.elementCount(); ++i)
        {
            auto e = path.elementAt(i);
            int x1 = (e.x - bounds.x()) * (1 << 16);
            int y1 = e.y * (1 << 16);
            switch (e.type)
            {
            case QPainterPath::ElementType::MoveToElement:
            {
                ui->info_text->append("MoveTo " + QString::number(x1) + ", "  + QString::number(y1));
                glyph_info.push_back(e.type);
                glyph_info.push_back(x1);
                glyph_info.push_back(y1);
                x = x1, y = y1;
                break;
            }
            case QPainterPath::ElementType::LineToElement:
            {
                ui->info_text->append("LineTo " + QString::number(x1) + ", "  + QString::number(y1));
                glyph_info.push_back(e.type);
                glyph_info.push_back(x1);
                glyph_info.push_back(y1);
                x = x1, y = y1;
                break;
            }
            case QPainterPath::ElementType::CurveToElement:
            {
                e = path.elementAt(++i);
                int x2 = (e.x - bounds.x()) * (1 << 16);
                int y2 = e.y * (1 << 16);
                e = path.elementAt(++i);
                int x3 = (e.x - bounds.x()) * (1 << 16);
                int y3 = e.y * (1 << 16);

                if (x == x1 && y == y1 && x2 == x3 && y2 == y3)
                {
                    //can be a lineto
                    ui->info_text->append("LineTo " + QString::number(x3) + ", "  + QString::number(y3));
                    glyph_info[len] -= 6 * sizeof(unsigned int);
                    glyph_info.push_back(QPainterPath::ElementType::LineToElement);
                }
                else
                {
                    glyph_info[len] -= 2 * sizeof(unsigned int);
                    ui->info_text->append("CurveTo " + QString::number(x1) + ", "  + QString::number(y1));
                    ui->info_text->append("CurveToData " + QString::number(x2) + ", "  + QString::number(y2));
                    ui->info_text->append("CurveToData " + QString::number(x3) + ", "  + QString::number(y3));
                    glyph_info.push_back(QPainterPath::ElementType::CurveToElement);
                    glyph_info.push_back(x1);
                    glyph_info.push_back(y1);
                    glyph_info.push_back(x2);
                    glyph_info.push_back(y2);
                }
                glyph_info.push_back(x3);
                glyph_info.push_back(y3);
                x = x3, y = y3;
                break;
            }
            default:
                break;
            }
        }
        glyph_paths.push_back(glyph_info);
        qApp->processEvents();
    }

    auto out_file = ui->path_text->text().toStdString();
    out_file = std::string(begin(out_file), begin(out_file) + out_file.find_last_of('.'));
    out_file += ".ctf";

    ui->info_text->append("-----------------------");
    ui->info_text->append("Outfile: " + QString::fromStdString(out_file));

    //write ctf file
    std::ofstream out;
    out.open (out_file, std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
    unsigned int ascent = raw_font.ascent() * (1 << 16);
    unsigned int descent = raw_font.descent() * (1 << 16);
    unsigned int start = ui->start_range->value();
    unsigned int end = ui->end_range->value();
    write(out, ascent);
    write(out, descent);
    write(out, end);
    write(out, start);
    for (int c = 0; c <= ui->end_range->value() - ui->start_range->value(); ++c)
    {
        unsigned int offset = 0;
        std::for_each(begin(glyph_paths), begin(glyph_paths) + c, [&] (auto &e) { offset += e.size(); });
        offset += 4 + ui->end_range->value() - ui->start_range->value() + 2;
        offset *= sizeof(unsigned int);
        write(out, offset);
        ui->info_text->append("Offset: " + QString::number(offset));
    }
    unsigned int zero = 0;
    write(out, zero);
    for (auto path : glyph_paths) for (auto e : path) write(out, e);
    out.close();
}
