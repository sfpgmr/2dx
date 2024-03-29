﻿/*
==============================================================================

Copyright 2005-11 by Satoshi Fujiwara.

async can be redistributed and/or modified under the terms of the
GNU General Public License, as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

async is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with async; if not, visit www.gnu.org/licenses or write to the
Free Software Foundation, Inc., 59 Temple Place, Suite 330, 
Boston, MA 02111-1307 USA

==============================================================================
*/
/* ToDo

TODO: リサイズに対応する

*/

#include "stdafx.h"
#include "resource.h"
#define BOOST_ASSIGN_MAX_PARAMS 7
#include <boost/assign.hpp>
#include <boost/assign/ptr_list_of.hpp>
#include <boost/assign/ptr_list_inserter.hpp>
#include <boost/foreach.hpp>

#include "sf_windows.h"
#include "dcomposition_window.h"
#include "CommDlg.h"
#include "icon.h"
#include "timer.h"
#include "exception.h"
#include "application.h"
#include "config_tab_dialog.h"
#include "info_tab_dialog.h"
#include "midi_config_tab_dialog.h"

#if _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif
using namespace DirectX;

namespace sf 
{


  struct simple_vertex
  {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 norm;
    DirectX::XMFLOAT2 tex;
  };

  struct cb_never_changes
  {
    DirectX::XMMATRIX mView;
    DirectX::XMFLOAT4 vLightDir;
  };

  struct cb_change_on_resize
  {
    DirectX::XMMATRIX mProjection;
  };

  struct cb_changes_every_frame
  {
    DirectX::XMMATRIX mWorld;
    DirectX::XMFLOAT4 vLightColor;

    //    DirectX::XMFLOAT4 vMeshColor;
  };

  struct dcomposition_window::impl : public base_win32_window_t
  {

    impl(const std::wstring& menu_name,const std::wstring& name,bool fit_to_display,float width = 160,float height = 100) 
      : base_win32_window_t(menu_name,name,fit_to_display,width,height) 
      , timer_(*this,100)
      , icon_(IDI_ICON1)
      /*,mesh_color_(0.7f, 0.7f, 0.7f, 1.0f)*/
      ,init_(false)
      ,thumb_start_(false)
    {
      fullscreen_ = false;
      //on_render.connect(boost::bind(&impl::render,this));
    };

    ~impl(){
      //safe_release(dxgi_factory_);
    };


    // -------------------------------------------------------------------
    // ウィンドウプロシージャ
    // -------------------------------------------------------------------

    virtual void create(){
      create_device_independent_resources();
      //    icon_ = ::LoadIconW(HINST_THISCOMPONENT,MAKEINTRESOURCE(IDI_ICON1));
      register_class(this->name_.c_str(),CS_HREDRAW | CS_VREDRAW ,0,0,icon_.get());
      create_window();
      text(name_);

      // 半透明ウィンドウを有効にする。
      //BOOL dwmEnable;
      //DwmIsCompositionEnabled (&dwmEnable); 
      //if (dwmEnable) EnableBlurBehind(*this);

    }

    void calc_client_size()
    {
      //クライアント領域の現在の幅、高さを求める
      RECT rc;
      GetClientRect( hwnd_, &rc );
      client_width_ = rc.right - rc.left;
      client_height_ = rc.bottom - rc.top;
    }

    LRESULT other_window_proc(HWND hwnd,uint32_t message, WPARAM wParam, LPARAM lParam)
    {
      return DefWindowProcW(hwnd,message,wParam,lParam);
    };

    virtual void create_device() 
    {
      base_win32_window_t::create_device();
     // create_dcomp_device();

    }

    void create_dcomp_device()
    {
      THROW_IF_ERR(DCompositionCreateDevice(dxgi_device_.Get(),IID_PPV_ARGS(&dcomp_device_)));
      THROW_IF_ERR(dcomp_device_->CreateTargetForHwnd(hwnd_,TRUE,&dcomp_target_));
      IDCompositionSurfacePtr dcomp_surf;
      RECT surf_rect = {0,0,200,200};
      THROW_IF_ERR(dcomp_device_->CreateSurface(surf_rect.right,surf_rect.bottom,swap_chain_desc_.Format,DXGI_ALPHA_MODE_PREMULTIPLIED,&dcomp_surf));
      IDXGISurfacePtr dxgi_surf;
      POINT offset;

      THROW_IF_ERR(dcomp_surf->BeginDraw(&surf_rect,IID_PPV_ARGS(&dxgi_surf),&offset));

      D2D1_BITMAP_PROPERTIES1 bitmap_prop = D2D1::BitmapProperties1( 
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, 
            D2D1::PixelFormat(swap_chain_desc_.Format, D2D1_ALPHA_MODE_PREMULTIPLIED), 
            dpi_.dpix(), 
            dpi_.dpiy()); 
 
      ID2D1ImagePtr backup;
      ID2D1Bitmap1Ptr d2dtarget_bitmap;
      THROW_IF_ERR(d2d_context_->CreateBitmapFromDxgiSurface(dxgi_surf.Get(), &bitmap_prop, &d2dtarget_bitmap)); 

      d2d_context_->GetTarget(&backup);
      d2d_context_->SetTarget(d2dtarget_bitmap.Get());

      ID2D1SolidColorBrushPtr brush,tbrush;
      float alpha = 1.0f;
      THROW_IF_ERR(d2d_context_->CreateSolidColorBrush(D2D1::ColorF(1.0f,0.0f,0.0f,alpha),&brush));
      THROW_IF_ERR(d2d_context_->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f),&tbrush));
      IDWriteTextFormatPtr format;
      // Text Formatの作成
      THROW_IF_ERR(write_factory_->CreateTextFormat(
        L"メイリオ",                // Font family name.
        NULL,                       // Font collection (NULL sets it to use the system font collection).
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.000f,
        L"ja-jp",
        & format));

      d2d_context_->BeginDraw();

      float w =  surf_rect.right / 2.0f,h = surf_rect.bottom / 2.0f;
      std::wstring t = L"DirectComposition1";

      d2d_context_->FillRectangle(D2D1::RectF(0.0f,0.0f,w,h),brush.Get());
      d2d_context_->DrawTextW(t.c_str(),t.size(),format.Get(),D2D1::RectF(0.0f,0.0f,w,h),tbrush.Get());

      brush->SetColor(D2D1::ColorF(0.0f,1.0f,0.0f,alpha));
      d2d_context_->FillRectangle(D2D1::RectF(w,0.0f,w + w,h),brush.Get());
      t = L"DirectComposition2";
      d2d_context_->DrawTextW(t.c_str(),t.size(),format.Get(),D2D1::RectF(w,0.0f,w + w,h),tbrush.Get());
      brush->SetColor(D2D1::ColorF(0.0f,0.0f,1.0f,alpha));
      t = L"DirectComposition4";
      d2d_context_->FillRectangle(D2D1::RectF(w,h,w + w,h + h),brush.Get());
      d2d_context_->DrawTextW(t.c_str(),t.size(),format.Get(),D2D1::RectF(w,h,w + w,h + h),tbrush.Get());
      brush->SetColor(D2D1::ColorF(1.0f,0.0f,1.0f,alpha));
      d2d_context_->FillRectangle(D2D1::RectF(0,h,w,h + h),brush.Get());
      t = L"DirectComposition3";
      d2d_context_->DrawTextW(t.c_str(),t.size(),format.Get(),D2D1::RectF(0,h,w,h + h),tbrush.Get());

      d2d_context_->EndDraw();
      dcomp_surf->EndDraw();
      brush.Reset();

      
      IDCompositionVisualPtr v,v1,v2,v3,v4;
      THROW_IF_ERR(dcomp_device_->CreateVisual(&v));
      THROW_IF_ERR(v->SetContent(dcomp_surf.Get()));

      //v->SetOffsetX(width_ / 2.0f);
      //v->SetOffsetY(height_ / 5.0f);

      dcomp_target_->SetRoot(v.Get());

      THROW_IF_ERR(dcomp_device_->CreateVisual(&v1));
      THROW_IF_ERR(v1->SetContent(dcomp_surf.Get()));
      THROW_IF_ERR(dcomp_device_->CreateVisual(&v2));
      THROW_IF_ERR(v2->SetContent(dcomp_surf.Get()));
      THROW_IF_ERR(dcomp_device_->CreateVisual(&v3));
      THROW_IF_ERR(v3->SetContent(dcomp_surf.Get()));
      THROW_IF_ERR(dcomp_device_->CreateVisual(&v4));
      THROW_IF_ERR(v4->SetContent(dcomp_surf.Get()));

      v1->SetOffsetY(height_  / 5.0f);
      v2->SetOffsetY(height_  / 5.0f - h);
      v3->SetOffsetY(height_  / 5.0f);
      v4->SetOffsetY(height_  / 5.0f - h);

      v1->SetOffsetX(w * 2.0f);
      v2->SetOffsetX(w * 3.0f);
      v3->SetOffsetX(w * 3.0f);
      v4->SetOffsetX(w * 4.0f);

      IDCompositionRectangleClipPtr clip;
      dcomp_device_->CreateRectangleClip(&clip);
      clip->SetLeft(0.0f);
      clip->SetRight(w-1.0f);
      clip->SetTop(0.0f);
      clip->SetBottom(h-1.0f);
      THROW_IF_ERR(clip->SetTopLeftRadiusX(3.0f));
      THROW_IF_ERR(clip->SetBottomLeftRadiusX(3.0f));

      //v1->SetClip(D2D1::RectF(0.0f,0.0f,w-1.0f,h-1.0f));
      v1->SetClip(clip.Get());
      v2->SetClip(D2D1::RectF(0.0f,h,w -1.0f,h + h -1.0f));
      v3->SetClip(D2D1::RectF(w,0.0f,w + w - 1.0f,h - 1.0f));
      v4->SetClip(D2D1::RectF(w,h,w+w - 1.0f,h+h - 1.0f));

      v->AddVisual(v1.Get(),FALSE,nullptr);
      v->AddVisual(v2.Get(),FALSE,nullptr);
      v->AddVisual(v3.Get(),FALSE,nullptr);
      v->AddVisual(v4.Get(),FALSE,nullptr);

      // 2D Transform のセットアップ
      {
        IDCompositionTransform* transforms[3];

        IDCompositionTransformPtr transform_group;

        THROW_IF_ERR(dcomp_device_->CreateRotateTransform(&rot_));
        THROW_IF_ERR(dcomp_device_->CreateRotateTransform(&rot_child_));
        THROW_IF_ERR(dcomp_device_->CreateScaleTransform(&scale_));
        THROW_IF_ERR(dcomp_device_->CreateTranslateTransform(&trans_));


        rot_->SetCenterX(w);
        rot_->SetCenterY(h);
        rot_->SetAngle(0.0f);

        rot_child_->SetCenterX(w/2.0f);
        rot_child_->SetCenterY(w/2.0f);

        scale_->SetCenterX(w);
        scale_->SetCenterY(h);
        scale_->SetScaleX(2.0f);
        scale_->SetScaleY(2.0f);

        trans_->SetOffsetX((width_ - surf_rect.right)/2.0f);
        trans_->SetOffsetY((height_ - surf_rect.bottom)/2.0f);

        transforms[0] = rot_.Get();
        transforms[1] = scale_.Get();
        transforms[2] = trans_.Get();

        THROW_IF_ERR(dcomp_device_->CreateTransformGroup(transforms,3,&transform_group));
        v->SetTransform(transform_group.Get());
        v1->SetTransform(rot_child_.Get());
        v2->SetTransform(rot_child_.Get());
        v3->SetTransform(rot_child_.Get());
        v4->SetTransform(rot_child_.Get());
      }

      {
        // Opacityのアニメーション
        IDCompositionAnimationPtr anim;
        THROW_IF_ERR(dcomp_device_->CreateAnimation(&anim));
        anim->AddCubic(0.0f,0.0f,0.5f / 4.0f,0.0f,0.0f);
        anim->AddCubic(4.0f,0.5f,-0.5f / 4.0f,0.0f,0.0f);
        anim->AddRepeat(8.0f,8.0f);
        //anim->End(10.0f,0.0f);

        IDCompositionEffectGroupPtr effect;
        dcomp_device_->CreateEffectGroup(&effect);
        effect->SetOpacity(anim.Get());

        IDCompositionAnimationPtr anim3d;
        THROW_IF_ERR(dcomp_device_->CreateAnimation(&anim3d));
        anim3d->AddCubic(0.0f,0.0f,360.0f / 8.0f,0.0f,0.0f);
        anim3d->AddRepeat(8.0f,8.0f);

        IDCompositionRotateTransform3DPtr rot3d;
        dcomp_device_->CreateRotateTransform3D(&rot3d);
        rot3d->SetAngle(anim3d.Get());
        rot3d->SetAxisZ(0.0f);
        rot3d->SetAxisY(0.0f);
        rot3d->SetAxisX(1.0f);
        rot3d->SetCenterX(w);
        rot3d->SetCenterY(w);

     //   rot3d->SetAxisX(1.0f);

        effect->SetTransform3D(rot3d.Get());

        // 3D変換のアニメーション

        v->SetEffect(effect.Get());
      }

      dcomp_device_->Commit();
     
 
      d2d_context_->SetTarget(backup.Get());

    }

    virtual void discard_device()
    {
      base_win32_window_t::discard_device();
      //discard_dcomp_device();
    }

    void discard_dcomp_device()
    {
      dcomp_device_.Reset();
    }

    void render(first_b2d& b2d)
    {
      if(activate_){
        b2World& world(b2d.world());

        static float rot = 0.0f;
        float color[4] = { 0.0f, 0.0f, 0.0f, 0.5f };    

        // 描画ターゲットのクリア
        d3d_context_->ClearRenderTargetView(d3d_render_target_view_.Get(),color);
        // 深度バッファのクリア
        d3d_context_->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0 );
/*
        // 色の変更
        mesh_color_.x = 1.0f;
        mesh_color_.y = 1.0f;
        mesh_color_.z = 1.0f;

        // 定数更新

        cb_changes_every_frame cb;
        static float rad = 0.0f;
        rad += 0.01f;
        mat_world_ = XMMatrixRotationY( rad );
        cb.mWorld = XMMatrixTranspose( mat_world_ );
        cb.vLightColor = mesh_color_;
        d3d_context_->UpdateSubresource( cb_changes_every_frame_.Get(), 0, NULL, &cb, 0, 0 );
        d3d_context_->OMSetRenderTargets( 1, d3d_render_target_view_.GetAddressOf(), depth_view_.Get() );

        // 四角形
        d3d_context_->VSSetShader( v_shader_.Get(), NULL, 0 );
        d3d_context_->VSSetConstantBuffers( 0, 1, cb_never_changes_.GetAddressOf() );
        d3d_context_->VSSetConstantBuffers( 1, 1, cb_change_on_resize_.GetAddressOf() );
        d3d_context_->VSSetConstantBuffers( 2, 1, cb_changes_every_frame_.GetAddressOf() );
        d3d_context_->PSSetShader( p_shader_.Get(), NULL, 0 );
        d3d_context_->PSSetConstantBuffers( 2, 1, cb_changes_every_frame_.GetAddressOf() );
        d3d_context_->PSSetShaderResources( 0, 1, shader_res_view_.GetAddressOf() );
        d3d_context_->PSSetSamplers( 0, 1, sampler_state_.GetAddressOf() );

        d3d_context_->DrawIndexed( 36, 0, 0 );
        */
        

        
        d2d_context_->BeginDraw();
        //d2d_context_->Clear();


        //thunk_proc_ = (WNDPROC)thunk_.getCode();
        D2D_RECT_F layout_rect_ = D2D1::RectF(0.0f,100.0f,width_,100.0f);
        // Text Formatの作成
        THROW_IF_ERR(write_factory_->CreateTextFormat(
          L"メイリオ",                // Font family name.
          NULL,                       // Font collection (NULL sets it to use the system font collection).
          DWRITE_FONT_WEIGHT_REGULAR,
          DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL,
          32.000f,
          L"ja-jp",
          &write_text_format_
          ));
        d2d_context_->SetTransform(D2D1::Matrix3x2F::Identity());
        ID2D1SolidColorBrushPtr brush,line_brush,obj_brush;
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::OrangeRed), &brush);
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(1.0f,1.0f,1.0f,0.5f), &line_brush);
        d2d_context_->CreateSolidColorBrush(D2D1::ColorF(0.5f,0.5f,1.0f,1.0f), &obj_brush);

        {
          D2D_POINT_2F start,end;
          for(float i = 0;i < width_+1.0f;i += 16.0f)
          {
            start.x = end.x = i;
            end.y = height_;
            start.y = 0.0f;
            d2d_context_->DrawLine(start,end,line_brush.Get(),0.5f);
          }

          for(float i = 0;i < height_+1.0f;i += 16.0f)
          {
            start.y = end.y = i;
            end.x = width_;
            start.x = 0.0f;
            d2d_context_->DrawLine(start,end,line_brush.Get(),0.5f);
          }

        }

        static int count;
        count++;

        // Box2Dオブジェクトの描画
        b2Body* body = world.GetBodyList();
        int32 body_count = world.GetBodyCount();
        //D2D1::Matrix3x2F mat = D2D1::Matrix3x2F::Identity();
        //mat._22 = -1.0f;
        //mat = mat * D2D1::Matrix3x2F::Scale(10.0f,10.0f);
        //D2D1::Matrix3x2F matt = D2D1::Matrix3x2F::Translation(width_ / 2.0f,height_ /2.0f);
        //mat._31 = matt._31;
        //mat._32 = matt._32;

        b2Mat22 mat;
 //       b2Mat33 mat;
        float32 scale = 5.0f;
        mat.SetZero();
        mat.ex.x = 1.0f * scale;
        mat.ey.y = -1.0f * scale;
        b2Vec2 screen;
        screen.x = width_ / 2.0f;
        screen.y = height_ / 2.0f;
//        float32 limit_y = -screen.y;
        //mat.ez.x = width_ / 2.0f;
        //mat.ez.y = height_ / 2.0f;

        for(int32 i = 0;i < body_count;++i){
          b2Fixture* fix = body->GetFixtureList();
          const b2Vec2& vb = body->GetPosition();
          while(fix){
            b2Shape* s = fix->GetShape();
            switch(s->GetType())
            {
            case  b2Shape::e_polygon:
              {
                ID2D1PathGeometry1Ptr geometry;
                //        ID2D1RectangleGeometryPtr 
                d2d_factory_->CreatePathGeometry(&geometry);
                ID2D1GeometrySinkPtr sink;
                geometry->Open(&sink);

                b2PolygonShape* sp = static_cast<b2PolygonShape*>(s);
                int32 vcount = sp->GetVertexCount();

                b2Vec2 v1 = sp->GetVertex(0);// + vb;
                v1 = b2Mul(body->GetTransform(),v1);
                v1 = b2Mul(mat,v1) + screen;


                sink->BeginFigure(D2D1::Point2F(v1.x ,v1.y),D2D1_FIGURE_BEGIN_FILLED);
                for(int j = 1;j < vcount;++j)
                {
                  b2Vec2 v = sp->GetVertex(j);// + vb;
                  v = b2Mul(body->GetTransform(),v);
                  v = b2Mul(mat,v) + screen;
                  sink->AddLine(D2D1::Point2F(v.x,v.y));
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();

                d2d_context_->FillGeometry(geometry.Get(),obj_brush.Get());

              }
              break;
            case  b2Shape::e_circle:
              {
                b2CircleShape* circle = static_cast<b2CircleShape*>(s);
                b2Vec2 center = b2Mul(mat,b2Mul(body->GetTransform(), circle->m_p));
                float32 radius = circle->m_radius * scale;
                b2Vec2 axis = b2Mul(body->GetTransform().q, b2Vec2(1.0f, 0.0f));
                d2d_context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x + screen.x,center.y + screen.y),radius,radius),obj_brush.Get());
              }
              break;
            case b2Shape::e_edge:
              {
                b2EdgeShape* edge = static_cast<b2EdgeShape*>(s);
                b2Vec2 v1 = b2Mul(mat,b2Mul(body->GetTransform(), edge->m_vertex1)) + screen;
                b2Vec2 v2 = b2Mul(mat,b2Mul(body->GetTransform(), edge->m_vertex2)) + screen;
                d2d_context_->DrawLine(D2D1::Point2F(v1.x ,v1.y),D2D1::Point2F(v2.x,v2.y),obj_brush.Get());

              }
              break;

            case b2Shape::e_chain:
              {
                ID2D1PathGeometry1Ptr geometry;
                //        ID2D1RectangleGeometryPtr 
                d2d_factory_->CreatePathGeometry(&geometry);
                ID2D1GeometrySinkPtr sink;
                geometry->Open(&sink);

                b2ChainShape* chain = static_cast<b2ChainShape*>(s);
                int32 count = chain->m_count;
                const b2Vec2* vertices = chain->m_vertices;


                b2Vec2 v1 = b2Mul(mat,b2Mul(body->GetTransform(), vertices[0])) + screen;
                sink->BeginFigure(D2D1::Point2F(v1.x ,v1.y),D2D1_FIGURE_BEGIN_FILLED);
                for (int32 i = 1; i < count; ++i)
                {
                  b2Vec2 v2 = b2Mul(mat,b2Mul(body->GetTransform(), vertices[i])) + screen;
                  sink->AddLine(D2D1::Point2F(v2.x,v2.y));
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                d2d_context_->DrawGeometry(geometry.Get(),obj_brush.Get());
              }
              break;
            }
            fix = fix->GetNext();
          }
          body = body->GetNext();
        }


        std::wstring m(L"Box2Dの処理結果をDirect2Dで描画する");
        d2d_context_->DrawTextW(
          m.c_str(),
          m.size(),
          write_text_format_.Get(),
          layout_rect_, 
          brush.Get());


        d2d_context_->EndDraw();

        // フリップ

        DXGI_PRESENT_PARAMETERS parameters = {};
        static int off = 0;
        POINT offset = {0,off--};
        RECT srect ={0,0,width_,height_};
        parameters.DirtyRectsCount = 0;
        parameters.pDirtyRects = nullptr;
        parameters.pScrollRect = nullptr;
        parameters.pScrollOffset = nullptr;
        if(FAILED(dxgi_swap_chain_->Present1(1,0,&parameters)))
        {
          restore_swapchain_and_dependent_resources();
        };
      }
    }


    void on_player_event(HWND wnd,UINT_PTR wParam)
    {
      //    application::instance()->Player()->HandleEvent(wParam);
    }

    sf::base_win32_window_t::result_t on_create(CREATESTRUCT *p)
    {
      create_device();
      timer_.start();
      return 0;
    }


    LRESULT on_size(uint32_t flag,uint32_t width,uint32_t height)
    {
      // バックバッファなどに関係するインターフェースを解放する
      // バックバッファを解放する
      if(init_)
      {
        int height = client_height_;
        int width = client_width_;

        calc_client_size();

      }
      // バックバッファなどに関係するインターフェースを再作成する

      //if (render_target_)
      //{
      //	D2D1_SIZE_U size;
      //	size.width = lParam & 0xFFFF;
      //	size.height = (lParam >> 16) & 0xFFFF; ;

      //	// Note: This method can fail, but it's okay to ignore the
      //	// error here -- it will be repeated on the next call to
      //	// EndDraw.
      //	render_target_->Resize(size);
      //}
      return TRUE;
    }

    LRESULT on_display_change(uint32_t bpp,uint32_t h_resolution,uint32_t v_resolution)
    {
      invalidate_rect();
      return TRUE;
    }

    LRESULT on_erase_backgroud(HDC dc)
    {
      return FALSE;
    }

    LRESULT on_hscroll(uint32_t state,uint32_t position,HWND ctrl_hwnd)
    {
      return FALSE;
    }

    LRESULT on_destroy()
    {
      ::PostQuitMessage(0);
      return FALSE;
    }

    LRESULT on_close()
    {
      //slider_.detatch();
      timer_.player_stop();
      // 後始末
      discard_device();
      // レンダーターゲットのリリース
      //safe_release(dcr_);
      //      safe_release(render_target_);
      // Windowの破棄
      discard_device_independant_resources();
      BOOL ret(::DestroyWindow(hwnd_));
      BOOST_ASSERT(ret != 0);
      return TRUE;
    }

    LRESULT on_command(uint32_t wparam, uint32_t lparam)
    {
      return FALSE;
    }

    LRESULT on_timer(uint32_t timer_id)
    {
      // TODO:スレッドのエラーチェックも入れておく
      //update();
      //InvalidateRect(hwnd_,NULL,FALSE);
    /*  static float angle = 0.0f;
      rot_->SetAngle(angle);
      rot_child_->SetAngle(360.0f - angle);
      angle += 10.0f;

      if(angle > 360.0f) angle -= 360.0f*/;

      //static float scale = 1.0f;
      //static float scale_add = 0.1f;

      //scale += scale_add;

      //if(scale > 10.0f) {
      //  scale  = 10.0f;
      //  scale_add = -scale_add;
      //} 

      //if(scale < 0.1f)
      //{
      //  scale = 0.1f;
      //  scale_add = -scale_add;
      //}

      //scale_->SetScaleX(scale);
      //scale_->SetScaleY(scale);

      //dcomp_device_->Commit();

      invalidate_rect();
      return TRUE;
    }

  private:

    timer timer_;
    bool thumb_start_;
    //slider slider_;

    float client_width_,client_height_;

    //IDXGISwapChainPtr dxgi_swap_chain_;
    // std::wstring dxgi_info_;

    icon icon_;
    bool init_;

    D2D1_SIZE_U icon_size_;
    IDCompositionDevicePtr dcomp_device_;
    IDCompositionTargetPtr dcomp_target_;
    IDCompositionRotateTransformPtr rot_,rot_child_;
    IDCompositionScaleTransformPtr scale_;
    IDCompositionTranslateTransformPtr trans_;

    // thisとhwndをつなぐthunkクラス
    // メンバー関数を直接呼び出す。
    struct hwnd_this_thunk2 : public Xbyak::CodeGenerator {
      hwnd_this_thunk2(LONG_PTR this_addr,const void * proc)
      {
        // 引数の位置をひとつ後ろにずらす
        mov(r10,r9);
        mov(r9,r8);
        mov(r8,rdx);
        mov(rdx,rcx);
        // thisのアドレスをrcxに格納する
        mov(rcx,(LONG_PTR)this_addr);
        // 第5引数をスタックに格納
        push(r10);
        sub(rsp,32);
        mov(r10,(LONG_PTR)proc);
        // メンバ関数呼び出し
        call(r10);
        add(rsp,40);
        ret(0);
      }
    };

    //hwnd_this_thunk2 thunk_info_;
    //  hwnd_this_thunk2 thunk_config_;
    //proc_t proc_info_;
    //  proc_t proc_config_;

  };

  dcomposition_window::dcomposition_window(const std::wstring& menu_name,const std::wstring& name,bool fit_to_display,float width ,float height)
    : impl_(new impl(menu_name,name,fit_to_display,width,height))
  {

  };

  void * dcomposition_window::raw_handle() const {return impl_->raw_handle();};
  void dcomposition_window::create(){impl_->create();};
  void dcomposition_window::show(){impl_->show();};
  void dcomposition_window::hide(){impl_->hide();};
  bool dcomposition_window::is_show(){return impl_->is_show();};
  void dcomposition_window::text(std::wstring& text){impl_->text(text);};
  void dcomposition_window::message_box(const std::wstring& text,const std::wstring& caption,uint32_t type )
  {
    impl_->message_box(text,caption,type);
  };
  void dcomposition_window::update(){impl_->update();};
  void dcomposition_window::render(first_b2d& b2d){impl_->render(b2d);};
  // void dcomposition_window::player_ready(){impl_->player_ready();};


  dcomposition_window_ptr create_dcomposition_window
    (
    const std::wstring& menu_name,
    const std::wstring& name,
    const uint32_t show_flag,
    bool fit_to_display,
    float width,
    float height
    )
  {
    // クライアント領域のサイズからウィンドウサイズを設定
    RECT    rect    = { 0, 0, width, height };
    //::AdjustWindowRectEx( &rect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE, WS_EX_APPWINDOW | WS_EX_TOPMOST );
    dcomposition_window* p = new dcomposition_window(menu_name,name,fit_to_display,rect.right - rect.left,rect.bottom - rect.top);
    p->create();
    p->show();
    p->update();
    return dcomposition_window_ptr(p);
  }

}

